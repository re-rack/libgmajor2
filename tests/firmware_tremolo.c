/* The Tremolo and Panner as the firmware runs them: the Filter block subtypes 5
 * and 6, one UI object X:0x0988CE, the activation P:0x09172F with its six
 * updaters, the ramp records P:0x09605F arms and the P:0x0001FD jobs for the
 * Hi-Cut pairs, and the audio continuation P:0x000E5A with its task record.
 */
#include "firmware_common.h"
#include "gm2_effect.h"
#include "gm2_emu.h"
#include "gm2_record.h"

#include <dsp56300.h>

#include <stdio.h>
#include <stdlib.h>

#define TREMOLO_ALGORITHM 1u
#define TREMOLO_UI 0x0988CEu
#define TREMOLO_RAW 0x099B46u /* X:0x099B36 + 16 * 1, Filter block raw index */
#define TREMOLO_TAP_TIME 0x099A6Bu
#define TREMOLO_SUBTYPE_INDEX 0u
#define TREMOLO_OUT_LEVEL_INDEX 14u
/* P:0x08F763 and the level struct at UI+36 it takes in R0. */
#define TREMOLO_LEVEL_CODE 0x08F763u
#define TREMOLO_LEVEL_STRUCT (TREMOLO_UI + 36u)
#define TREMOLO_KILL_INDEX 15u
#define TREMOLO_STAGED 0x09887Au   /* pulse, step, lag, rise, fall, depth, Hi-Cut pairs */
#define TREMOLO_JOB_DATA 0x098884u /* the block's records: payloads, then list entries */
#define TREMOLO_JOB_DATA_WORDS 0x80u
#define TREMOLO_LEVEL 0x03C1u
#define TREMOLO_LEVEL_STAGED 0x098905u
#define TREMOLO_ACTIVATION 0x09172Fu
#define TREMOLO_RATE_UPDATER 0x0918CEu
#define TREMOLO_SPEED_INDEX 4u
#define TREMOLO_SINE_GENERATOR 0x093820u
#define TREMOLO_SINE_SCRATCH 0x0F00u
#define TREMOLO_AUDIO_ENTRY 0x000E5Au
#define TREMOLO_TASK 0x03A8u
#define TREMOLO_AUDIO_IN 0x0600u
#define TREMOLO_AUDIO_OUT 0x0620u
#define TREMOLO_WET_POINTER 0x03C2u
#define TREMOLO_LINK 0x070Bu
#define TREMOLO_AUDIO_BUDGET 400000
#define TREMOLO_DESCRIPTOR_STRIDE 15u
#define TREMOLO_DESCRIPTOR_FORMAT 12u
#define TREMOLO_JOBS_MAX 16u
#define TREMOLO_KNOBS_MAX 7u
/* The tempo flag P:0x09172F passes to the rate updater from M0, which holds the
 * 0xFFFFFF the audio routines load into every M register: the synced rate path. */
#define TREMOLO_TEMPO_FLAG 0xFFFFFFu
/* The value P:0x083268 leaves at X:0x09860C for a callback's second call. */
#define TREMOLO_EDIT_VALUE 0x09860Cu
#define TREMOLO_EDIT_SECOND 0x50u

/* The words the block's records move: pulse, step, lag, rise, fall, depth, the two
 * Hi-Cut pairs, then the level job's three. */
static const uint32_t k_targets[] = {0x03B3u, 0x03B4u, 0x03B6u, 0x03B7u,      0x03B8u,
                                     0x03B9u, 0x03BCu, 0x03BFu, TREMOLO_LEVEL};

static const uint32_t k_handlers[] = {GM2_JOB_LINEAR, GM2_JOB_SMOOTH};

static const gm2_firmware_job_spec k_jobs = {
    .name = "lfo",
    .handlers = k_handlers,
    .handler_count = sizeof k_handlers / sizeof k_handlers[0],
    .data = TREMOLO_JOB_DATA,
    .data_words = TREMOLO_JOB_DATA_WORDS,
    .targets = k_targets,
    .target_count = sizeof k_targets / sizeof k_targets[0],
    .cap = TREMOLO_JOBS_MAX,
};

struct gm2_firmware {
  gm2_emu *emu;
  unsigned subtype;
  unsigned knobs;
  const gm2_knob *knob;
  uint32_t callback[TREMOLO_KNOBS_MAX];
  uint32_t format[TREMOLO_KNOBS_MAX];
  uint32_t raw_index[TREMOLO_KNOBS_MAX];
  int32_t values[TREMOLO_KNOBS_MAX];
  unsigned phase;
  int audio_dirty;
};

/* One activation: X:(UI+1) = 1 installs the level struct and queues the block's
 * jobs, the second one with 0 settles the out level. */
static int tremolo_activate(gm2_emu *emu, unsigned first) {
  gm2_write(emu, GM2_X, TREMOLO_UI + 1u, first);
  gm2_set_reg(emu, DSP56300_REG_A2, 0);
  gm2_set_reg(emu, DSP56300_REG_A1, TREMOLO_ALGORITHM);
  gm2_set_reg(emu, DSP56300_REG_A0, 0);
  gm2_set_reg(emu, DSP56300_REG_R0, TREMOLO_UI);
  gm2_set_reg(emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  if (gm2_call(emu, TREMOLO_ACTIVATION, GM2_FIRMWARE_CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "lfo: activation P:0x%06X did not return\n", TREMOLO_ACTIVATION);
    return -1;
  }
  return 0;
}

/* The block's level code stages its three words at X:0x098905 once both
 * activations have run. */
static int tremolo_level_words(unsigned subtype, uint32_t levels[3]) {
  gm2_emu *emu = gm2_firmware_boot("lfo");
  if (emu == NULL) {
    return -1;
  }
  int status = -1;
  if (gm2_filter_subtype(emu, subtype) == 0) {
    gm2_write(emu, GM2_X, TREMOLO_UI, TREMOLO_ALGORITHM);
    gm2_write(emu, GM2_X, TREMOLO_RAW + TREMOLO_SUBTYPE_INDEX, subtype);
    gm2_write(emu, GM2_X, TREMOLO_RAW + TREMOLO_OUT_LEVEL_INDEX, 0);
    if (tremolo_activate(emu, 1u) == 0 && tremolo_activate(emu, 0u) == 0) {
      for (unsigned i = 0; i < 3u; i++) {
        levels[i] = gm2_read(emu, GM2_X, TREMOLO_LEVEL_STAGED + i);
      }
      status = 0;
    }
  }
  gm2_destroy(emu);
  return status;
}

/* The block selected as the device selects it, with the subtype's sub-record in
 * the shared P/X view the descriptor accessor reads. */
static int tremolo_select(gm2_firmware *self) {
  gm2_emu *emu = self->emu;
  if (gm2_filter_subtype(emu, self->subtype) != 0) {
    return -1;
  }
  gm2_write(emu, GM2_X, TREMOLO_UI, TREMOLO_ALGORITHM);
  gm2_write(emu, GM2_X, TREMOLO_RAW + TREMOLO_SUBTYPE_INDEX, self->subtype);
  gm2_write(emu, GM2_X, TREMOLO_RAW + TREMOLO_KILL_INDEX, 0);
  gm2_write(emu, GM2_X, TREMOLO_TAP_TIME, GM2_EFFECT_BEAT_MS);
  return 0;
}

/* The descriptor entries the effect's knobs sit at, against the knob table. The
 * sub-record's table word is the mirror gm2_filter_subtype installs: the subtype
 * updater rewrites the P side on the device's shared memory. */
static int tremolo_descriptor(gm2_firmware *self, const unsigned *index, const char *who) {
  uint32_t record = gm2_read(self->emu, GM2_X, GM2_CHAIN + TREMOLO_ALGORITHM);
  uint32_t table = gm2_read(self->emu, GM2_X, record + 7u);
  if (record == 0u) {
    fprintf(stderr, "%s: the Filter block has no descriptor record\n", who);
    return -1;
  }
  for (unsigned knob = 0; knob < self->knobs; knob++) {
    self->raw_index[knob] = index[knob];
    if (gm2_firmware_knob_entry(self->emu, table, index[knob], &self->knob[knob],
                                &self->callback[knob]) != 0) {
      return -1;
    }
    self->format[knob] =
        gm2_read(self->emu, GM2_X,
                 table + TREMOLO_DESCRIPTOR_STRIDE * index[knob] + TREMOLO_DESCRIPTOR_FORMAT);
  }
  return 0;
}

/* The waveform tables the sine wave interpolates; the Soft and Hard waves read
 * none of them. */
static int tremolo_waves(gm2_firmware *self) {
  for (uint32_t word = 0; word < 8u; word++) {
    gm2_write(self->emu, GM2_X, TREMOLO_SINE_SCRATCH + word, 0);
  }
  gm2_set_reg(self->emu, DSP56300_REG_R0, TREMOLO_SINE_SCRATCH);
  gm2_set_reg(self->emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  if (gm2_call(self->emu, TREMOLO_SINE_GENERATOR, GM2_FIRMWARE_CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "lfo: waveform generator P:0x%06X did not return\n", TREMOLO_SINE_GENERATOR);
    return -1;
  }
  return 0;
}

/* The rate updater on the synced path: what the activation runs, and the only
 * state a Speed turn would otherwise leave dependent on the edit's order, since
 * the Speed callback P:0x091A38 forces the Ignore path. */
static int tremolo_rate_rebuild(gm2_firmware *self) {
  gm2_set_reg(self->emu, DSP56300_REG_A2, 0);
  gm2_set_reg(self->emu, DSP56300_REG_A1, TREMOLO_TEMPO_FLAG);
  gm2_set_reg(self->emu, DSP56300_REG_A0, 0);
  gm2_set_reg(self->emu, DSP56300_REG_R0, TREMOLO_UI);
  gm2_set_reg(self->emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  if (gm2_call(self->emu, TREMOLO_RATE_UPDATER, GM2_FIRMWARE_CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "lfo: rate updater P:0x%06X did not return\n", TREMOLO_RATE_UPDATER);
    return -1;
  }
  return 0;
}

/* The level code P:0x08F763 on the struct at UI+36, which the Out Level callback
 * P:0x091A1D reaches only when the block's gate word X:(UI+3) is clear; neither
 * activation pass writes that word. */
static int tremolo_level_rebuild(gm2_firmware *self) {
  gm2_set_reg(self->emu, DSP56300_REG_A2, 0);
  gm2_set_reg(self->emu, DSP56300_REG_A1, 0);
  gm2_set_reg(self->emu, DSP56300_REG_A0, 0);
  gm2_set_reg(self->emu, DSP56300_REG_R0, TREMOLO_LEVEL_STRUCT);
  gm2_set_reg(self->emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  if (gm2_call(self->emu, TREMOLO_LEVEL_CODE, GM2_FIRMWARE_CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "lfo: level code P:0x%06X did not return\n", TREMOLO_LEVEL_CODE);
    return -1;
  }
  return 0;
}

/* The knob vector is the host's: the rate updater's write-back of the nearest
 * Speed step is put back without running its own updater. */
static void tremolo_restore_raw(gm2_firmware *self) {
  for (unsigned knob = 0; knob < self->knobs; knob++) {
    gm2_write(self->emu, GM2_X, TREMOLO_RAW + self->raw_index[knob],
              (uint32_t)self->values[knob] & 0xFFFFFFu);
  }
}

/* The records the block holds are the ones the port models: six P:0x0001B9 ramps
 * P:0x09605F arms, two P:0x0001FD one-poles, over the block's own data, and every
 * word the kernel reads from a record has its record. */
static int tremolo_check_jobs(const gm2_firmware *self) {
  return gm2_firmware_check_jobs(self->emu, &k_jobs);
}

/* The device's parameter edit for one knob, P:0x083268: the raw word, the
 * descriptor's own callback with A = the algorithm and B = the value, a second
 * call with A = 0x50 and the value at X:0x09860C when the format word has bits 23
 * and 13 set (the Tempo entry has them), then the knob vector's raw words put back
 * and the synced rate the activation rebuilds after a Speed turn. */
static int tremolo_set_knob(gm2_firmware *self, unsigned knob, int value) {
  self->values[knob] = (int32_t)value;
  gm2_write(self->emu, GM2_X, TREMOLO_RAW + self->raw_index[knob], (uint32_t)value & 0xFFFFFFu);
  gm2_set_reg(self->emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  gm2_set_reg(self->emu, DSP56300_REG_B2, 0);
  gm2_set_reg(self->emu, DSP56300_REG_B1, (uint32_t)value & 0xFFFFFFu);
  gm2_set_reg(self->emu, DSP56300_REG_B0, 0);
  const uint32_t calls[2] = {TREMOLO_ALGORITHM, TREMOLO_EDIT_SECOND};
  const unsigned passes = gm2_firmware_second_call(self->format[knob]) ? 2u : 1u;
  for (unsigned pass = 0; pass < passes; pass++) {
    if (pass != 0u) {
      gm2_write(self->emu, GM2_X, TREMOLO_EDIT_VALUE, (uint32_t)value & 0xFFFFFFu);
    }
    gm2_set_reg(self->emu, DSP56300_REG_A2, 0);
    gm2_set_reg(self->emu, DSP56300_REG_A1, calls[pass]);
    gm2_set_reg(self->emu, DSP56300_REG_A0, 0);
    if (gm2_call(self->emu, self->callback[knob], GM2_FIRMWARE_CALL_BUDGET, NULL) != GM2_CALL_OK) {
      fprintf(stderr, "lfo: callback P:0x%06X did not return for knob %u\n", self->callback[knob],
              knob);
      return -1;
    }
  }
  if (tremolo_check_jobs(self) != 0) {
    return -1;
  }
  /* The Tempo rule: the knob values alone decide the sound, so the synced rate
   * the activation computes follows a Speed turn, whose own path is Ignore. */
  if (self->raw_index[knob] == TREMOLO_SPEED_INDEX && tremolo_rate_rebuild(self) != 0) {
    return -1;
  }
  if (self->raw_index[knob] == TREMOLO_OUT_LEVEL_INDEX && tremolo_level_rebuild(self) != 0) {
    return -1;
  }
  tremolo_restore_raw(self);
  /* The callbacks clobber the registers the continuation runs with. */
  self->audio_dirty = 1;
  return 0;
}

/* The continuation's record: the state and word bases the activation installed,
 * then the input, wet, dry and output pointers and the link record. */
static void tremolo_task_record(gm2_emu *emu) {
  const uint32_t pointers[5] = {TREMOLO_AUDIO_IN, TREMOLO_WET_POINTER, TREMOLO_AUDIO_IN,
                                TREMOLO_AUDIO_OUT, TREMOLO_LINK};
  gm2_write_run(emu, GM2_Y, TREMOLO_TASK + 6u, pointers, 5);
  gm2_firmware_link(emu, TREMOLO_LINK, TREMOLO_AUDIO_OUT);
}

static void tremolo_audio_begin(gm2_emu *emu) {
  gm2_firmware_audio_reset(emu);
  gm2_set_reg(emu, DSP56300_REG_R6, TREMOLO_TASK);
  tremolo_task_record(emu);
}

static int tremolo_frame(gm2_firmware *self, int32_t left, int32_t right, int32_t *out_left,
                         int32_t *out_right) {
  if (self->audio_dirty != 0) {
    tremolo_audio_begin(self->emu);
    /* Only the registers the callback clobbered; the chunk cadence keeps counting. */
    self->audio_dirty = 0;
  }
  gm2_write(self->emu, GM2_X, TREMOLO_AUDIO_IN, gm2_word24(left));
  gm2_write(self->emu, GM2_X, TREMOLO_AUDIO_IN + 1u, gm2_word24(right));
  gm2_set_reg(self->emu, DSP56300_REG_R6, TREMOLO_TASK);
  if (gm2_run_until_stop(self->emu, TREMOLO_AUDIO_ENTRY, TREMOLO_AUDIO_BUDGET, NULL) !=
      GM2_CALL_OK) {
    return -1;
  }
  *out_left = gm2_sign24(gm2_read(self->emu, GM2_X, TREMOLO_AUDIO_OUT));
  *out_right = gm2_sign24(gm2_read(self->emu, GM2_X, TREMOLO_AUDIO_OUT + 1u));
  /* The routine has no chunk logic of its own, so the jobs run on their own
   * counter. */
  self->phase = (self->phase + 1u) & 3u;
  if (self->phase == 0u) {
    return gm2_firmware_run_jobs(self->emu, "lfo", TREMOLO_JOBS_MAX);
  }
  return 0;
}

GM2_FIRMWARE_CLOSE(tremolo_close)

static gm2_firmware *tremolo_shared_open(const gm2_effect *effect, unsigned subtype,
                                         const unsigned *index, const char *who) {
  gm2_firmware *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->subtype = subtype;
  self->knobs = effect->knobs;
  self->knob = effect->knob;
  self->emu = gm2_firmware_boot(who);
  if (self->emu == NULL || tremolo_select(self) != 0 || tremolo_descriptor(self, index, who) != 0) {
    tremolo_close(self);
    return NULL;
  }
  for (unsigned knob = 0; knob < self->knobs; knob++) {
    self->values[knob] = effect->knob[knob].def;
    gm2_write(self->emu, GM2_X, TREMOLO_RAW + self->raw_index[knob],
              (uint32_t)self->values[knob] & 0xFFFFFFu);
  }
  if (tremolo_waves(self) != 0 || tremolo_activate(self->emu, 1u) != 0 ||
      tremolo_activate(self->emu, 0u) != 0 || tremolo_check_jobs(self) != 0 ||
      gm2_firmware_settle_words(self->emu, "lfo", TREMOLO_JOBS_MAX) != 0) {
    tremolo_close(self);
    return NULL;
  }
  tremolo_restore_raw(self);
  self->audio_dirty = 1;
  return self;
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int tremolo_levels(gm2_firmware *self, int32_t level[3]) {
  uint32_t words[3];
  if (tremolo_level_words(self->subtype, words) != 0) {
    return -1;
  }
  for (unsigned i = 0; i < 3u; i++) {
    if (words[i] != gm2_read(self->emu, GM2_X, TREMOLO_LEVEL_STAGED + i)) {
      fprintf(stderr, "lfo: staged level word %u is %06X, the activation gives %06X\n", i,
              gm2_read(self->emu, GM2_X, TREMOLO_LEVEL_STAGED + i), words[i]);
      return -1;
    }
    level[i] = gm2_sign24(words[i]);
  }
  return 0;
}

/* A setup word: staged in X, or written straight to its live word in Y. */
typedef struct {
  uint32_t address;
  enum gm2_space space;
} tremolo_word;

/* The Tremolo's setup words: the staged pulse, step, lag, rise, fall and depth,
 * the waveform word and the smoother, then the two staged Hi-Cut pairs. */
static const tremolo_word k_tremolo_words[] = {
    {TREMOLO_STAGED + 0u, GM2_X}, /* pulse */
    {TREMOLO_STAGED + 1u, GM2_X}, /* step */
    {0x03B5u, GM2_Y},         /* wave */
    {TREMOLO_STAGED + 2u, GM2_X}, /* lag */
    {TREMOLO_STAGED + 3u, GM2_X}, /* rise */
    {TREMOLO_STAGED + 4u, GM2_X}, /* fall */
    {TREMOLO_STAGED + 5u, GM2_X}, /* depth */
    {0x03BAu, GM2_Y},         /* smoother */
    {0x03BBu, GM2_Y},
    {TREMOLO_STAGED + 6u, GM2_X}, /* left Hi-Cut */
    {TREMOLO_STAGED + 7u, GM2_X},
    {TREMOLO_STAGED + 8u, GM2_X}, /* right Hi-Cut */
    {TREMOLO_STAGED + 9u, GM2_X},
};

/* The Panner's: the same without the pulse, rise and fall words, which its sine
 * waveform never reads. */
static const tremolo_word k_panner_words[] = {
    {TREMOLO_STAGED + 1u, GM2_X}, /* step */
    {0x03B5u, GM2_Y},         /* wave */
    {TREMOLO_STAGED + 2u, GM2_X}, /* lag */
    {TREMOLO_STAGED + 5u, GM2_X}, /* depth */
    {0x03BAu, GM2_Y},         /* smoother */
    {0x03BBu, GM2_Y},
    {TREMOLO_STAGED + 6u, GM2_X}, /* left Hi-Cut */
    {TREMOLO_STAGED + 7u, GM2_X},
    {TREMOLO_STAGED + 8u, GM2_X}, /* right Hi-Cut */
    {TREMOLO_STAGED + 9u, GM2_X},
};

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int tremolo_words(gm2_firmware *self, int32_t *words) {
  const int panner = self->subtype == 6u;
  const tremolo_word *list = panner ? k_panner_words : k_tremolo_words;
  const size_t count = panner ? sizeof k_panner_words / sizeof k_panner_words[0]
                              : sizeof k_tremolo_words / sizeof k_tremolo_words[0];
  for (size_t word = 0; word < count; word++) {
    words[word] = gm2_sign24(gm2_read(self->emu, list[word].space, list[word].address));
  }
  return 0;
}

static const unsigned k_tremolo_index[7] = {4u, 5u, 6u, 7u, 8u, 11u, TREMOLO_OUT_LEVEL_INDEX};
static const unsigned k_panner_index[4] = {4u, 5u, 6u, TREMOLO_OUT_LEVEL_INDEX};

static gm2_firmware *tremolo_open(void) {
  return tremolo_shared_open(&gm2_effect_tremolo, 5u, k_tremolo_index, "tremolo");
}

static gm2_firmware *panner_open(void) {
  return tremolo_shared_open(&gm2_effect_panner, 6u, k_panner_index, "panner");
}

const gm2_firmware_ops gm2_firmware_tremolo = {
    .effect = &gm2_effect_tremolo,
    .open = tremolo_open,
    .levels = tremolo_levels,
    .set_knob = tremolo_set_knob,
    .words = tremolo_words,
    .frame = tremolo_frame,
    .close = tremolo_close,
};

const gm2_firmware_ops gm2_firmware_panner = {
    .effect = &gm2_effect_panner,
    .open = panner_open,
    .levels = tremolo_levels,
    .set_knob = tremolo_set_knob,
    .words = tremolo_words,
    .frame = tremolo_frame,
    .close = tremolo_close,
};
