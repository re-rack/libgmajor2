/* The Gate/EQ block as the firmware runs it: the per-band updater P:0x0906B6
 * through the descriptor's own callbacks, the block activation P:0x09038B that
 * stages the band words and queues the block's jobs, the jobs themselves through
 * P:0x0001CD's record layout, and the audio continuation P:0x000CC0 with its task
 * record.
 */
#include "firmware_common.h"
#include "gm2_effect.h"
#include "gm2_emu.h"
#include "gm2_eq.h"
#include "gm2_record.h"

#include <dsp56300.h>

#include <stdio.h>
#include <stdlib.h>

#define EQ_ALGORITHM 6u
#define EQ_UI 0x0987A4u
#define EQ_PARAMS 0x099B36u
#define EQ_PARAM_STRIDE 16u
#define EQ_ACTIVE_INDEX 4u
#define EQ_FIRST_BAND_PARAM 5u
#define EQ_RAW (EQ_PARAMS + EQ_ALGORITHM * EQ_PARAM_STRIDE + EQ_FIRST_BAND_PARAM)
#define EQ_COEFF_X 0x098771u /* X: the staged words P:0x0906B6 publishes */
#define EQ_COEFF_Y 0x0385u   /* Y: the live words the kernel reads */
#define EQ_COEFFS_PER_BAND 5u
#define EQ_BAND_WORDS (EQ_COEFFS_PER_BAND * GM2_EQ_BANDS)
#define EQ_LEVEL 0x0394u /* Y: the input, wet and dry words the level code targets */
#define EQ_LEVEL_WORDS 3u
#define EQ_LEVEL_STAGED 0x0987BDu /* X: the three the level code P:0x08F763 stages */
#define EQ_ACTIVATION 0x09038Bu
/* The job entries the block queues its records under: the five-word group
 * P:0x00026F each band owns, and the single-word steps the level code's and the
 * gate's records run. A payload is one of the block's own records,
 * X:0x09874C..X:0x0987BF. */
#define EQ_JOB_DATA 0x09874Cu
#define EQ_JOB_DATA_WORDS 0x74u
#define EQ_JOB_SRC 0u
#define EQ_JOB_SNAP 3u
#define EQ_JOB_STEP 4u
#define EQ_JOB_SNAP_WORD 1
#define EQ_JOB_STEP_WORD 0x008371
#define EQ_JOBS_MAX 16u
#define EQ_SETTLE_ROUNDS 20000u
#define EQ_CHUNK_FRAMES 4u
#define EQ_STACK 0x1000u
#define EQ_CALL_BUDGET 20000000
#define EQ_AUDIO_BUDGET 400000
#define EQ_AUDIO_ENTRY 0x000CC0u
#define EQ_TASK 0x0700u
#define EQ_AUDIO_IN 0x0600u
#define EQ_AUDIO_OUT 0x0620u
#define EQ_AUDIO_STAGE 0x02A7u
#define EQ_STATE 0x0050u

static const uint32_t k_handlers[] = {GM2_JOB_GROUP, GM2_JOB_SMOOTH, GM2_JOB_LINEAR};

static const gm2_firmware_job_spec k_jobs = {
    .name = "eq",
    .handlers = k_handlers,
    .handler_count = sizeof k_handlers / sizeof k_handlers[0],
    .data = EQ_JOB_DATA,
    .data_words = EQ_JOB_DATA_WORDS,
    .cap = EQ_JOBS_MAX,
};

struct gm2_firmware {
  gm2_emu *emu;
  uint32_t callback[GM2_EQ_BANDS];
  unsigned chunk_pos;
  int audio_dirty;
};

/* The block selected as the device selects it, and the band object the
 * descriptor points at. */
static int eq_select(gm2_emu *emu) {
  gm2_write(emu, GM2_X, EQ_UI, EQ_ALGORITHM);
  gm2_write(emu, GM2_X, EQ_UI + 4u, 1);
  gm2_write(emu, GM2_X, EQ_UI + 9u, 0);
  gm2_write(emu, GM2_X, EQ_RAW - EQ_FIRST_BAND_PARAM + EQ_ACTIVE_INDEX, 1);
  if (gm2_mirror_descriptor(emu, EQ_ALGORITHM) < 0) {
    return -1;
  }
  if (gm2_read(emu, GM2_X, EQ_UI + 29u) != EQ_COEFF_X) {
    fprintf(stderr, "eq: the band object is X:0x%06X, not X:0x%06X\n",
            gm2_read(emu, GM2_X, EQ_UI + 29u), EQ_COEFF_X);
    return -1;
  }
  return 0;
}

/* The descriptor's name, clamp window and callback for every knob, against the
 * knob table: a firmware whose table moved cannot be recorded against this
 * effect. The three knobs of a band share one callback. */
static int eq_descriptor(const gm2_emu *emu, uint32_t callback[GM2_EQ_BANDS]) {
  uint32_t record = gm2_read(emu, GM2_X, GM2_CHAIN + EQ_ALGORITHM);
  uint32_t table = gm2_read(emu, GM2_P, record + 7u);
  uint32_t count = gm2_read(emu, GM2_P, record + 1u);
  if (record == 0u || count < EQ_FIRST_BAND_PARAM + gm2_effect_eq.knobs) {
    fprintf(stderr, "eq: descriptor X:0x%06X has %u parameters\n", record, count);
    return -1;
  }
  for (unsigned band = 0; band < GM2_EQ_BANDS; band++) {
    callback[band] = 0;
  }
  for (unsigned knob = 0; knob < gm2_effect_eq.knobs; knob++) {
    uint32_t address = 0;
    if (gm2_firmware_knob_entry(emu, table, EQ_FIRST_BAND_PARAM + knob, &gm2_effect_eq.knob[knob],
                                &address) != 0) {
      return -1;
    }
    unsigned band = knob / 3u;
    if (callback[band] != 0u && callback[band] != address) {
      fprintf(stderr, "eq: %s has callback P:0x%06X\n", gm2_effect_eq.knob[knob].name, address);
      return -1;
    }
    callback[band] = address;
  }
  return 0;
}

/* The device's path for a knob turn: the raw value and the parameter's callback,
 * which restages the band's five words at X:0x098771+5k and leaves the live ones
 * at Y:0x0385+5k to the block's job. */
static int eq_set_knob(gm2_firmware *self, unsigned knob, int value) {
  unsigned band = knob / 3u;
  gm2_write(self->emu, GM2_X, EQ_RAW + knob, (uint32_t)value & 0xFFFFFFu);
  if (gm2_firmware_edit(self->emu, EQ_ALGORITHM, self->callback[band], value, EQ_STACK) != 0) {
    fprintf(stderr, "eq: updater P:0x%06X did not return for knob %u\n", self->callback[band],
            knob);
    return -1;
  }
  /* The updater call clobbers the registers the continuation runs with. */
  self->audio_dirty = 1;
  return 0;
}

/* The block activation: every parameter updater, the block's level code through
 * P:0x090736/P:0x090747/P:0x090758, and the seven records P:0x096194 queues. */
static int eq_activate(gm2_firmware *self) {
  gm2_write(self->emu, GM2_X, EQ_UI + 1u, 1);
  gm2_set_reg(self->emu, DSP56300_REG_A2, 0);
  gm2_set_reg(self->emu, DSP56300_REG_A1, EQ_ALGORITHM);
  gm2_set_reg(self->emu, DSP56300_REG_A0, 0);
  gm2_set_reg(self->emu, DSP56300_REG_R0, EQ_UI);
  gm2_set_reg(self->emu, DSP56300_REG_R7, EQ_STACK);
  if (gm2_call(self->emu, EQ_ACTIVATION, EQ_CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "eq: activation P:0x%06X did not return\n", EQ_ACTIVATION);
    return -1;
  }
  return 0;
}

/* The rest state: the level code's three words glided up from their power-on
 * zero, and every band word on its staged word. */
static int eq_settle(gm2_firmware *self) {
  for (unsigned round = 0; round < EQ_SETTLE_ROUNDS; round++) {
    if (gm2_firmware_run_jobs(self->emu, "eq", EQ_JOBS_MAX) != 0) {
      return -1;
    }
    unsigned same = 0;
    for (unsigned word = 0; word < EQ_BAND_WORDS; word++) {
      same += gm2_read(self->emu, GM2_Y, EQ_COEFF_Y + word) ==
              gm2_read(self->emu, GM2_X, EQ_COEFF_X + word);
    }
    for (unsigned word = 0; word < EQ_LEVEL_WORDS; word++) {
      same += gm2_read(self->emu, GM2_Y, EQ_LEVEL + word) ==
              gm2_read(self->emu, GM2_X, EQ_LEVEL_STAGED + word);
    }
    if (same == EQ_BAND_WORDS + EQ_LEVEL_WORDS) {
      return 0;
    }
  }
  fprintf(stderr, "eq: the staged words never settled\n");
  return -1;
}

/* The record that moves band `band`'s five words. */
static int eq_band_job(const gm2_emu *emu, const uint32_t *record, unsigned count, unsigned band) {
  const uint32_t source = EQ_COEFF_X + EQ_COEFFS_PER_BAND * band;
  const uint32_t target = EQ_COEFF_Y + EQ_COEFFS_PER_BAND * band;
  for (unsigned i = 0; i < count; i++) {
    const uint32_t handler = gm2_read(emu, GM2_X, record[i] + GM2_JOB_HANDLER);
    const uint32_t params = gm2_read(emu, GM2_X, record[i] + GM2_JOB_PARAMS);
    if (handler != GM2_JOB_GROUP || gm2_read(emu, GM2_X, params + EQ_JOB_SRC) != source ||
        gm2_read(emu, GM2_X, params + GM2_JOB_DST) != target ||
        gm2_read(emu, GM2_X, params + GM2_JOB_COUNT) != EQ_COEFFS_PER_BAND ||
        gm2_sign24(gm2_read(emu, GM2_X, params + EQ_JOB_SNAP)) != EQ_JOB_SNAP_WORD ||
        gm2_read(emu, GM2_X, params + EQ_JOB_STEP) != EQ_JOB_STEP_WORD) {
      continue;
    }
    return 0;
  }
  fprintf(stderr, "eq: no job moves X:0x%06X onto Y:0x%06X\n", source, target);
  return -1;
}

/* The block's jobs are the three band groups and nothing else the adapter does
 * not run: every record it holds is one of the block's own entries. */
static int eq_check_jobs(const gm2_firmware *self) {
  uint32_t record[EQ_JOBS_MAX];
  unsigned count = 0;
  if (gm2_firmware_read_jobs(self->emu, &k_jobs, record, &count) != 0) {
    return -1;
  }
  for (unsigned band = 0; band < GM2_EQ_BANDS; band++) {
    if (eq_band_job(self->emu, record, count, band) != 0) {
      return -1;
    }
  }
  return 0;
}

static void eq_task_record(gm2_emu *emu) {
  const uint32_t record[8] = {
      EQ_AUDIO_IN,    /* r3, stereo input */
      EQ_LEVEL,       /* r4, Y input gain */
      EQ_AUDIO_STAGE, /* r2, X staging area */
      EQ_STATE,       /* r1, L biquad state */
      EQ_COEFF_Y,     /* r5, Y band coefficients */
      EQ_LEVEL + 1u,  /* r5 for the output stage, the wet gain */
      EQ_AUDIO_OUT,   /* r1 for the output stage */
      EQ_TASK + 8u,   /* the record the chain continues with */
  };
  gm2_write_run(emu, GM2_Y, EQ_TASK, record, 8);
  gm2_firmware_link(emu, EQ_TASK + 8u, EQ_AUDIO_OUT);
}

static void eq_audio_begin(gm2_emu *emu) {
  gm2_firmware_audio_reset(emu);
  gm2_set_reg(emu, DSP56300_REG_R6, EQ_TASK);
  eq_task_record(emu);
}

/* The continuation, then the block's jobs once the frame completes a chunk. */
static int eq_frame(gm2_firmware *self, int32_t left, int32_t right, int32_t *out_left,
                    int32_t *out_right) {
  if (self->audio_dirty != 0) {
    eq_audio_begin(self->emu);
    self->audio_dirty = 0;
  }
  gm2_write(self->emu, GM2_X, EQ_AUDIO_IN, gm2_word24(left));
  gm2_write(self->emu, GM2_X, EQ_AUDIO_IN + 1u, gm2_word24(right));
  gm2_set_reg(self->emu, DSP56300_REG_R6, EQ_TASK);
  if (gm2_run_until_stop(self->emu, EQ_AUDIO_ENTRY, EQ_AUDIO_BUDGET, NULL) != GM2_CALL_OK) {
    return -1;
  }
  *out_left = gm2_sign24(gm2_read(self->emu, GM2_X, EQ_AUDIO_OUT));
  *out_right = gm2_sign24(gm2_read(self->emu, GM2_X, EQ_AUDIO_OUT + 1u));
  if (++self->chunk_pos < EQ_CHUNK_FRAMES) {
    return 0;
  }
  self->chunk_pos = 0;
  return gm2_firmware_run_jobs(self->emu, "eq", EQ_JOBS_MAX);
}

static void eq_close(gm2_firmware *self) {
  if (self != NULL) {
    gm2_destroy(self->emu);
    free(self);
  }
}

static gm2_firmware *eq_open(void) {
  gm2_firmware *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->emu = gm2_firmware_boot("eq");
  if (self->emu == NULL || eq_select(self->emu) != 0 ||
      eq_descriptor(self->emu, self->callback) != 0) {
    eq_close(self);
    return NULL;
  }
  for (unsigned knob = 0; knob < gm2_effect_eq.knobs; knob++) {
    if (eq_set_knob(self, knob, gm2_effect_eq.knob[knob].def) != 0) {
      eq_close(self);
      return NULL;
    }
  }
  if (eq_activate(self) != 0) {
    eq_close(self);
    return NULL;
  }
  eq_audio_begin(self->emu);
  self->audio_dirty = 0;
  if (eq_check_jobs(self) != 0 || eq_settle(self) != 0) {
    eq_close(self);
    return NULL;
  }
  return self;
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int eq_levels(gm2_firmware *self, int32_t level[3]) {
  for (unsigned word = 0; word < EQ_LEVEL_WORDS; word++) {
    uint32_t live = gm2_read(self->emu, GM2_Y, EQ_LEVEL + word);
    uint32_t staged = gm2_read(self->emu, GM2_X, EQ_LEVEL_STAGED + word);
    if (live != staged) {
      fprintf(stderr, "eq: level word Y:0x%06X is %06X, X:0x%06X stages %06X\n", EQ_LEVEL + word,
              live, EQ_LEVEL_STAGED + word, staged);
      return -1;
    }
    level[word] = gm2_sign24(live);
  }
  return 0;
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int eq_words(gm2_firmware *self, int32_t *words) {
  for (unsigned word = 0; word < gm2_effect_eq.setup_words; word++) {
    words[word] = gm2_sign24(gm2_read(self->emu, GM2_X, EQ_COEFF_X + word));
  }
  return 0;
}

const gm2_firmware_ops gm2_firmware_eq = {
    .effect = &gm2_effect_eq,
    .open = eq_open,
    .levels = eq_levels,
    .set_knob = eq_set_knob,
    .words = eq_words,
    .frame = eq_frame,
    .close = eq_close,
};
