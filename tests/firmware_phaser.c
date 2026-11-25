/* The Modulation block's two Phasers as the firmware runs them: the block's own
 * parameter edit P:0x083268, which writes the raw word and calls the entry's
 * descriptor callback, the ten job records the block holds after its activation
 * (nine that publish the staged words to the live ones and the level code's own),
 * and the audio continuation P:0x000FA8 with its task record Y:0x0420.
 *
 * The LFO reads the unipolar sine table at X:0x1000, which P:0x093820 builds. No
 * knob touches the three level words, so open() runs the level job until they
 * have settled and the run starts at rest, as the native open() does.
 */
#include "firmware_common.h"
#include "gm2_effect.h"
#include "gm2_emu.h"
#include "gm2_record.h"

#include <dsp56300.h>

#include <stdio.h>
#include <stdlib.h>

#define PHASER_ALGORITHM 3u
#define PHASER_EDIT 0x083268u  /* the device's parameter edit: A1 = row + index, B1 = value */
#define PHASER_EDIT_BASE 0x30u /* the Modulation block's row in that id space */
#define PHASER_UI 0x098A8Du
#define PHASER_ACTIVATION 0x092B92u
#define PHASER_LFO_STEP 0x092EDAu
#define PHASER_SINE 0x093820u
#define PHASER_SINE_ARGS 0x0700u
#define PHASER_PARAMS 0x099B66u
#define PHASER_PARAM_WORDS 16u
#define PHASER_MIX_INDEX 13u
#define PHASER_OUT_LEVEL_INDEX 14u
#define PHASER_TAP_SOURCE 0x099A66u
#define PHASER_BEAT 0x099A6Bu
#define PHASER_LEVEL 0x0459u
#define PHASER_TASK 0x0420u
#define PHASER_INPUT 0x0050u
#define PHASER_OUTPUT 0x0052u
#define PHASER_LINK 0x041Eu
#define PHASER_JOBS_MAX 16u
/* X: the payload records of the staged block's jobs, S+22 of the staged words up
 * to the record the level code's own job names. */
#define PHASER_JOB_DATA 0x098A35u
#define PHASER_JOB_DATA_WORDS 0x33u
/* Y: the two live words the staged block's linear records ramp. */
#define PHASER_LIVE_STEP 0x0433u
#define PHASER_LIVE_DEPTH 0x0434u
#define PHASER_KERNEL 0x000FA8u
#define PHASER_CHUNK_FRAMES 4u
/* The level code's record, which publishes the three words it stages at
 * X:0x098AC4: the params {source, destination, count, snap, step} at UI+49. */
#define PHASER_LEVEL_PARAMS (PHASER_UI + 49u)
#define PHASER_LEVEL_STAGED (PHASER_UI + 55u)
#define PHASER_LEVEL_WORDS 3u
#define PHASER_ENTRY_STRIDE 15u
#define PHASER_ENTRY_DEFAULT 13u
#define PHASER_ACTIVATION_BUDGET 20000000
#define PHASER_SETTLE_MAX 8000

/* The staged words S+0..21 less the two tap weights no subtype sets, which the
 * port does not hold; a setting that staged one would have to be ported. */
#define PHASER_STAGED_WORDS 22u
#define PHASER_TAP_LEFT_UNSET 15u
#define PHASER_TAP_RIGHT_UNSET 18u

/* A1 the edit's second callback call passes; P:0x092EDA then takes the Tempo path,
 * which derives the step from the Tempo word and writes the nearest Speed back. */
#define PHASER_TEMPO_ARG 0x50u

/* The knob table's order: only these two feed the LFO step word. */
enum { PHASER_KNOB_SPEED = 0, PHASER_KNOB_TEMPO = 2 };

/* The two subtypes share every word above; they differ in the subtype the block
 * runs and so in the staged words and the knob table. */
typedef struct {
  unsigned subtype;
} phaser_block;

struct gm2_firmware {
  const gm2_effect *effect;
  const phaser_block *block;
  gm2_emu *emu;
  int32_t values[9];
  uint32_t record[PHASER_JOBS_MAX]; /* the job records the block holds */
  unsigned records;
  int audio_dirty;
  unsigned chunk_pos;
};

static const phaser_block k_blocks[] = {
    [0] = {.subtype = 9u},  /* Vintage Phaser */
    [1] = {.subtype = 10u}, /* Smooth Phaser */
};

static const uint32_t k_handlers[] = {GM2_JOB_LINEAR, GM2_JOB_SMOOTH};

static const gm2_firmware_job_spec k_jobs = {
    .name = "phaser",
    .handlers = k_handlers,
    .handler_count = sizeof k_handlers / sizeof k_handlers[0],
    .data = PHASER_JOB_DATA,
    .data_words = PHASER_JOB_DATA_WORDS,
    .other_params = PHASER_LEVEL_PARAMS,
    .cap = PHASER_JOBS_MAX,
};

static int phaser_check_jobs(const gm2_firmware *self);
static int phaser_take_jobs(gm2_firmware *self);
static int phaser_jobs(gm2_firmware *self);

static void phaser_audio_begin(gm2_emu *emu) { gm2_firmware_audio_reset(emu); }

/* The block's own raw words: the knobs of `self->values`, the subtype, and Out
 * Level 0 dB. */
static void phaser_raws(gm2_firmware *self) {
  const int32_t *k = self->values;
  int32_t raw[PHASER_PARAM_WORDS] = {0};
  raw[0] = (int32_t)self->block->subtype;
  raw[1] = k[0];  /* Speed */
  raw[3] = k[1];  /* Depth */
  raw[4] = k[2];  /* Tempo */
  raw[6] = k[3];  /* Feedback */
  raw[11] = k[4]; /* Range */
  raw[12] = k[5]; /* Phase Reverse */
  raw[PHASER_MIX_INDEX] = k[6];
  raw[PHASER_OUT_LEVEL_INDEX] = 0;
  for (unsigned i = 0; i < PHASER_PARAM_WORDS; i++) {
    gm2_write(self->emu, GM2_X, PHASER_PARAMS + i, gm2_word24(raw[i]));
  }
}

static uint32_t phaser_staged(const gm2_emu *emu) { return gm2_read(emu, GM2_X, PHASER_UI + 58u); }

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int phaser_levels(gm2_firmware *self, int32_t level[3]) {
  const uint32_t staged = PHASER_LEVEL_STAGED;
  for (unsigned i = 0; i < PHASER_LEVEL_WORDS; i++) {
    level[i] = gm2_sign24(gm2_read(self->emu, GM2_X, staged + i));
  }
  return 0;
}

/* The live level words settle onto the staged ones by the block's own record, so
 * open() runs the list until they have: the run then starts at rest, as the native
 * open() does. */
static int phaser_settle_levels(gm2_firmware *self) {
  for (int round = 0; round < PHASER_SETTLE_MAX; round++) {
    unsigned settled = 0;
    for (unsigned i = 0; i < PHASER_LEVEL_WORDS; i++) {
      settled += gm2_read(self->emu, GM2_X, PHASER_LEVEL_STAGED + i) ==
                         gm2_read(self->emu, GM2_Y, PHASER_LEVEL + i)
                     ? 1u
                     : 0u;
    }
    if (settled == PHASER_LEVEL_WORDS) {
      return 0;
    }
    if (phaser_jobs(self) != 0) {
      return -1;
    }
  }
  fprintf(stderr, "phaser: the level words never settled\n");
  return -1;
}

/* One descriptor entry per knob: the firmware's name, clamp window and callback,
 * and the entry's own default word where it lies inside that window. */
static int phaser_descriptor(gm2_firmware *self) {
  const uint32_t record = gm2_read(self->emu, GM2_X, GM2_CHAIN + PHASER_ALGORITHM);
  const uint32_t table = gm2_read(self->emu, GM2_P, record + 7u);
  const uint32_t count = gm2_read(self->emu, GM2_P, record + 1u);
  if (record == 0u || table == 0u || count < 13u) {
    fprintf(stderr, "phaser: descriptor X:0x%06X of P:0x%06X has %u parameters\n", record, table,
            count);
    return -1;
  }
  static const unsigned k_index[7] = {1u, 3u, 4u, 6u, 11u, 12u, PHASER_MIX_INDEX};
  for (unsigned knob = 0; knob < self->effect->knobs; knob++) {
    const gm2_knob *want = &self->effect->knob[knob];
    uint32_t callback = 0;
    if (gm2_firmware_knob_entry(self->emu, table, k_index[knob], want, &callback) != 0) {
      return -1;
    }
    const int32_t entry = gm2_sign24(gm2_read(
        self->emu, GM2_P, table + PHASER_ENTRY_STRIDE * k_index[knob] + PHASER_ENTRY_DEFAULT));
    if (entry >= want->min && entry <= want->max && entry != want->def) {
      fprintf(stderr, "phaser: %s defaults to %d in the descriptor, %d in the knob table\n",
              want->name, (int)entry, want->def);
      return -1;
    }
  }
  return 0;
}

/* The block as the device activates it: the subtype's sub-record, the LFO's sine
 * table, the raw words, the link the routing layer would hand the block, and the
 * two activations, the first staging the level words and the second computing them
 * from Mix and Out Level. */
static int phaser_prepare(gm2_firmware *self) {
  gm2_emu *emu = self->emu;
  phaser_audio_begin(emu);
  if (gm2_mod_subtype(emu, self->block->subtype) != 0) {
    return -1;
  }
  for (uint32_t i = 0; i < 8u; i++) {
    gm2_write(emu, GM2_X, PHASER_SINE_ARGS + i, 0u);
  }
  gm2_set_reg(emu, DSP56300_REG_A2, 0);
  gm2_set_reg(emu, DSP56300_REG_A1, 0);
  gm2_set_reg(emu, DSP56300_REG_A0, 0);
  gm2_set_reg(emu, DSP56300_REG_R0, PHASER_SINE_ARGS);
  if (gm2_firmware_call(emu, PHASER_SINE, GM2_FIRMWARE_CALL_BUDGET) != 0) {
    fprintf(stderr, "phaser: the sine generator P:0x%06X did not return\n", PHASER_SINE);
    return -1;
  }
  phaser_raws(self);
  gm2_write(emu, GM2_X, PHASER_TAP_SOURCE, 1u);
  gm2_write(emu, GM2_X, PHASER_BEAT, GM2_EFFECT_BEAT_MS);
  gm2_write(emu, GM2_X, PHASER_UI + 32u, PHASER_INPUT);
  gm2_write(emu, GM2_X, PHASER_UI + 33u, PHASER_OUTPUT);
  gm2_write(emu, GM2_X, PHASER_UI + 34u, PHASER_LINK);
  gm2_firmware_link(emu, PHASER_LINK, PHASER_OUTPUT);
  for (unsigned pass = 0; pass < 2u; pass++) {
    gm2_write(emu, GM2_X, PHASER_UI + 1u, pass == 0u ? 1u : 0u);
    gm2_write(emu, GM2_X, PHASER_UI + 3u, 0u);
    gm2_set_reg(emu, DSP56300_REG_A2, 0);
    gm2_set_reg(emu, DSP56300_REG_A1, PHASER_ALGORITHM);
    gm2_set_reg(emu, DSP56300_REG_A0, 0);
    gm2_set_reg(emu, DSP56300_REG_R0, PHASER_UI);
    if (gm2_firmware_call(emu, PHASER_ACTIVATION, PHASER_ACTIVATION_BUDGET) != 0) {
      fprintf(stderr, "phaser: the activation P:0x%06X did not return\n", PHASER_ACTIVATION);
      return -1;
    }
  }
  if (phaser_descriptor(self) != 0 || phaser_take_jobs(self) != 0) {
    return -1;
  }
  return phaser_settle_levels(self);
}

static void phaser_close(gm2_firmware *self) {
  if (self != NULL) {
    gm2_destroy(self->emu);
    free(self);
  }
}

static gm2_firmware *phaser_open(const gm2_effect *effect, const phaser_block *block) {
  gm2_firmware *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->effect = effect;
  self->block = block;
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    self->values[knob] = effect->knob[knob].def;
  }
  self->emu = gm2_firmware_boot(effect->name);
  if (self->emu == NULL || phaser_prepare(self) != 0) {
    phaser_close(self);
    return NULL;
  }
  self->audio_dirty = 1;
  return self;
}

static gm2_firmware *vintphaser_open(void) {
  return phaser_open(&gm2_effect_vintphaser, &k_blocks[0]);
}

static gm2_firmware *smoothphaser_open(void) {
  return phaser_open(&gm2_effect_smoothphaser, &k_blocks[1]);
}

/* The device's whole parameter edit, then the host's knob vector back in the raw
 * words the callback rewrote (the Tempo path writes the nearest Speed back).
 *
 * A Speed turn with Tempo set is the one edit whose result depends on the order
 * of the turns: P:0x092F8F clears A before it calls the step updater, so the
 * updater reads the Speed and stages a Speed-derived step whatever the Tempo
 * says. The Tempo callback's own path, P:0x092FBA, is the one that derives the
 * step from the Tempo, so it re-derives the word from the vector; it writes the
 * nearest Speed back in turn, hence the second restore. */
static int phaser_set_knob(gm2_firmware *self, unsigned knob, int value) {
  static const unsigned k_index[7] = {1u, 3u, 4u, 6u, 11u, 12u, PHASER_MIX_INDEX};
  self->values[knob] = (int32_t)value;
  if (gm2_firmware_edit(self->emu, PHASER_EDIT_BASE + k_index[knob], PHASER_EDIT, value,
                        GM2_DEVICE_STACK) != 0) {
    fprintf(stderr, "phaser: the parameter edit P:0x%06X did not return\n", PHASER_EDIT);
    return -1;
  }
  phaser_raws(self);
  gm2_write(self->emu, GM2_X, PHASER_TAP_SOURCE, 1u);
  gm2_write(self->emu, GM2_X, PHASER_BEAT, GM2_EFFECT_BEAT_MS);
  if (knob == PHASER_KNOB_SPEED && self->values[PHASER_KNOB_TEMPO] != 0) {
    gm2_set_reg(self->emu, DSP56300_REG_A2, 0);
    gm2_set_reg(self->emu, DSP56300_REG_A1, PHASER_TEMPO_ARG);
    gm2_set_reg(self->emu, DSP56300_REG_A0, 0);
    gm2_set_reg(self->emu, DSP56300_REG_R0, PHASER_UI);
    if (gm2_firmware_call(self->emu, PHASER_LFO_STEP, PHASER_ACTIVATION_BUDGET) != 0) {
      fprintf(stderr, "phaser: the LFO step P:0x%06X did not return\n", PHASER_LFO_STEP);
      return -1;
    }
    phaser_raws(self);
  }
  self->audio_dirty = 1;
  return phaser_check_jobs(self);
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int phaser_words(gm2_firmware *self, int32_t *words) {
  const uint32_t staged = phaser_staged(self->emu);
  unsigned at = 0;
  for (unsigned word = 0; word < PHASER_STAGED_WORDS; word++) {
    const int32_t value = gm2_sign24(gm2_read(self->emu, GM2_X, staged + word));
    if (word != PHASER_TAP_LEFT_UNSET && word != PHASER_TAP_RIGHT_UNSET) {
      words[at++] = value;
    } else if (value != 0) {
      fprintf(stderr, "%s: the staged tap weight S+%u is %06X, not zero\n", self->effect->name,
              word, (unsigned)value & 0xFFFFFFu);
      return -1;
    }
  }
  return 0;
}

/* The block's own records and nothing else, after every knob turn as after the
 * activation: a turn that adds a record would leave it unrun. */
static int phaser_check_jobs(const gm2_firmware *self) {
  return gm2_firmware_check_same_jobs(self->emu, &k_jobs, self->record, self->records);
}

/* The records the block holds after its activation, kept so every chunk runs the
 * firmware's own list and not a list of this adapter's making. The three a knob
 * turn moves must be in it: the two one-word linear records P:0x092EDA and
 * P:0x092EAF arm, and the level code's record. */
static int phaser_take_jobs(gm2_firmware *self) {
  const uint32_t staged = phaser_staged(self->emu);
  uint32_t step = 0;
  uint32_t level = 0;
  if (gm2_firmware_read_jobs(self->emu, &k_jobs, self->record, &self->records) != 0) {
    return -1;
  }
  for (unsigned i = 0; i < self->records; i++) {
    const uint32_t handler = gm2_read(self->emu, GM2_X, self->record[i] + GM2_JOB_HANDLER);
    const uint32_t params = gm2_read(self->emu, GM2_X, self->record[i] + GM2_JOB_PARAMS);
    const uint32_t source = gm2_read(self->emu, GM2_X, params);
    const uint32_t target = gm2_read(self->emu, GM2_X, params + GM2_JOB_DST);
    const uint32_t count = gm2_read(self->emu, GM2_X, params + GM2_JOB_COUNT);
    if (params == PHASER_LEVEL_PARAMS) {
      level += handler == GM2_JOB_SMOOTH && source == PHASER_LEVEL_STAGED &&
                       target == PHASER_LEVEL && count == PHASER_LEVEL_WORDS
                   ? 1u
                   : 0u;
      continue;
    }
    if (handler != GM2_JOB_LINEAR || count != 1u) {
      continue;
    }
    if (target == PHASER_LIVE_STEP && source == staged) {
      step++;
    }
    if (target == PHASER_LIVE_DEPTH && source == staged + 1u) {
      step++;
    }
  }
  if (step != 2u || level != 1u) {
    fprintf(stderr, "phaser: the block's records do not move its step, depth and level words\n");
    return -1;
  }
  return 0;
}

/* Each record {previous, one, its payload, its handler}, run the way P:0x0001CD
 * runs it: R0 on the payload, then the handler. */
static int phaser_jobs(gm2_firmware *self) {
  return gm2_firmware_run_list(self->emu, self->record, self->records);
}

static int phaser_frame(gm2_firmware *self, int32_t left, int32_t right, int32_t *out_left,
                        int32_t *out_right) {
  gm2_emu *emu = self->emu;
  if (self->audio_dirty != 0) {
    phaser_audio_begin(emu);
    self->audio_dirty = 0;
  }
  gm2_write(emu, GM2_X, PHASER_INPUT, gm2_word24(left));
  gm2_write(emu, GM2_X, PHASER_INPUT + 1u, gm2_word24(right));
  gm2_set_reg(emu, DSP56300_REG_R6, PHASER_TASK + 1u);
  gm2_set_reg(emu, DSP56300_REG_M6, 0xFFFFFFu);
  if (gm2_run_until_stop(emu, PHASER_KERNEL, GM2_FIRMWARE_CALL_BUDGET, NULL) != GM2_CALL_OK) {
    return -1;
  }
  *out_left = gm2_sign24(gm2_read(emu, GM2_X, PHASER_OUTPUT));
  *out_right = gm2_sign24(gm2_read(emu, GM2_X, PHASER_OUTPUT + 1u));
  self->chunk_pos++;
  if (self->chunk_pos == PHASER_CHUNK_FRAMES) {
    self->chunk_pos = 0;
    return phaser_jobs(self);
  }
  return 0;
}

const gm2_firmware_ops gm2_firmware_vintphaser = {
    .effect = &gm2_effect_vintphaser,
    .open = vintphaser_open,
    .levels = phaser_levels,
    .set_knob = phaser_set_knob,
    .words = phaser_words,
    .frame = phaser_frame,
    .close = phaser_close,
};

const gm2_firmware_ops gm2_firmware_smoothphaser = {
    .effect = &gm2_effect_smoothphaser,
    .open = smoothphaser_open,
    .levels = phaser_levels,
    .set_knob = phaser_set_knob,
    .words = phaser_words,
    .frame = phaser_frame,
    .close = phaser_close,
};
