/* The Modulation block's two Univibes as the firmware runs them: the block's own
 * parameter edit P:0x083268, the nine job records the block holds after its
 * activation (eight that publish the staged words to the live ones and the level
 * code's own), and the audio continuation P:0x00107F with its task record Y:0x045C.
 * The routine's slow path runs on the first frame of each four-frame chunk, which it
 * reads from the interrupt handler's countdown X:0x0001.
 *
 * The LFO reads the bipolar sine table at Y:0x0100, which P:0x083A02 builds. No
 * knob touches the three level words -- Mix reaches its own staged words instead,
 * its callback pinning the block's mix word at full before it re-stages them --
 * so open() runs the level job until they have settled and the run starts at rest,
 * as the native open() does.
 */
#include "firmware_common.h"
#include "gm2_effect.h"
#include "gm2_emu.h"
#include "gm2_record.h"

#include <dsp56300.h>

#include <stdio.h>
#include <stdlib.h>

#define UNIVIBE_ALGORITHM 3u
#define UNIVIBE_EDIT 0x083268u
#define UNIVIBE_EDIT_BASE 0x30u
#define UNIVIBE_UI 0x098B7Au
#define UNIVIBE_ACTIVATION 0x09311Bu
#define UNIVIBE_LFO_STEP 0x09349Cu
#define UNIVIBE_SINE 0x083A02u
#define UNIVIBE_PARAMS 0x099B66u
#define UNIVIBE_PARAM_WORDS 16u
#define UNIVIBE_MIX_INDEX 13u
#define UNIVIBE_OUT_LEVEL_INDEX 14u
#define UNIVIBE_TAP_SOURCE 0x099A66u
#define UNIVIBE_BEAT 0x099A6Bu
#define UNIVIBE_LEVEL 0x0497u
#define UNIVIBE_TASK 0x045Cu
#define UNIVIBE_INPUT 0x0050u
#define UNIVIBE_OUTPUT 0x0052u
#define UNIVIBE_LINK 0x041Eu
#define UNIVIBE_CHUNK_WORD 0x0001u /* the countdown the slow path tests bit 2 of */
#define UNIVIBE_JOBS_MAX 16u
/* X: the payload records of the staged block's jobs, from the staged block's end
 * up to the record the level code's own job names. */
#define UNIVIBE_JOB_DATA 0x098B2Bu
#define UNIVIBE_JOB_DATA_WORDS 0x2Eu
/* Y: the two live words the staged block's linear record ramps. */
#define UNIVIBE_LIVE_DEPTH 0x0471u
#define UNIVIBE_KERNEL 0x00107Fu
#define UNIVIBE_CHUNK_FRAMES 4u
/* The level code's record, which publishes the three words it stages at
 * X:0x098BB1: the params {source, destination, count, snap, step} at UI+49. */
#define UNIVIBE_LEVEL_PARAMS (UNIVIBE_UI + 49u)
#define UNIVIBE_LEVEL_STAGED (UNIVIBE_UI + 55u) /* where the level code stages them */
#define UNIVIBE_LEVEL_WORDS 3u
#define UNIVIBE_SPEED_WORD 0x046Cu /* the target LFO speed, written directly */
#define UNIVIBE_POLY 0x0473u       /* the four waveshaper words, written directly */
#define UNIVIBE_ENTRY_STRIDE 15u
#define UNIVIBE_ENTRY_DEFAULT 13u
#define UNIVIBE_ACTIVATION_BUDGET 20000000
#define UNIVIBE_SETTLE_MAX 8000

#define UNIVIBE_TEMPO_ARG 0x50u /* A1 the edit's second callback call passes */

/* The knob table's order: the four the LFO step updater P:0x09349C serves. */
enum {
  UNIVIBE_KNOB_SPEED_1 = 0,
  UNIVIBE_KNOB_SPEED_2 = 1,
  UNIVIBE_KNOB_TEMPO_2 = 4,
  UNIVIBE_KNOB_SELECT = 7
};

typedef struct {
  unsigned subtype;
} univibe_block;

struct gm2_firmware {
  const gm2_effect *effect;
  const univibe_block *block;
  gm2_emu *emu;
  int32_t values[11];
  uint32_t record[UNIVIBE_JOBS_MAX]; /* the job records the block holds */
  unsigned records;
  int audio_dirty;
  unsigned chunk_pos;
};

static const univibe_block k_blocks[] = {
    [0] = {.subtype = 11u}, /* Vintage Univibe */
    [1] = {.subtype = 12u}, /* Modern Univibe */
};

static const uint32_t k_handlers[] = {GM2_JOB_LINEAR, GM2_JOB_SMOOTH};

static const gm2_firmware_job_spec k_jobs = {
    .name = "univibe",
    .handlers = k_handlers,
    .handler_count = sizeof k_handlers / sizeof k_handlers[0],
    .data = UNIVIBE_JOB_DATA,
    .data_words = UNIVIBE_JOB_DATA_WORDS,
    .other_params = UNIVIBE_LEVEL_PARAMS,
    .cap = UNIVIBE_JOBS_MAX,
};

static int univibe_check_jobs(const gm2_firmware *self);
static int univibe_take_jobs(gm2_firmware *self);
static int univibe_jobs(gm2_firmware *self);

static void univibe_audio_begin(gm2_emu *emu) { gm2_firmware_audio_reset(emu); }

static void univibe_raws(gm2_firmware *self) {
  const int32_t *k = self->values;
  int32_t raw[UNIVIBE_PARAM_WORDS] = {0};
  raw[0] = (int32_t)self->block->subtype;
  raw[1] = k[0];  /* Speed 1 */
  raw[2] = k[1];  /* Speed 2 */
  raw[3] = k[2];  /* Depth */
  raw[4] = k[3];  /* Tempo 1 */
  raw[5] = k[4];  /* Tempo 2 */
  raw[8] = k[5];  /* Attack */
  raw[9] = k[6];  /* Release */
  raw[10] = k[7]; /* Select */
  raw[12] = k[8]; /* Phase Reverse */
  raw[UNIVIBE_MIX_INDEX] = k[9];
  raw[UNIVIBE_OUT_LEVEL_INDEX] = k[10];
  for (unsigned i = 0; i < UNIVIBE_PARAM_WORDS; i++) {
    gm2_write(self->emu, GM2_X, UNIVIBE_PARAMS + i, gm2_word24(raw[i]));
  }
}

static uint32_t univibe_staged(const gm2_emu *emu) {
  return gm2_read(emu, GM2_X, UNIVIBE_UI + 58u);
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int univibe_levels(gm2_firmware *self, int32_t level[3]) {
  const uint32_t staged = UNIVIBE_LEVEL_STAGED;
  for (unsigned i = 0; i < UNIVIBE_LEVEL_WORDS; i++) {
    level[i] = gm2_sign24(gm2_read(self->emu, GM2_X, staged + i));
  }
  return 0;
}

/* The live level words settle onto the staged ones by the block's own record, so
 * open() runs the list until they have: the run then starts at rest, as the native
 * open() does. */
static int univibe_settle_levels(gm2_firmware *self) {
  for (int round = 0; round < UNIVIBE_SETTLE_MAX; round++) {
    unsigned settled = 0;
    for (unsigned i = 0; i < UNIVIBE_LEVEL_WORDS; i++) {
      settled += gm2_read(self->emu, GM2_X, UNIVIBE_LEVEL_STAGED + i) ==
                         gm2_read(self->emu, GM2_Y, UNIVIBE_LEVEL + i)
                     ? 1u
                     : 0u;
    }
    if (settled == UNIVIBE_LEVEL_WORDS) {
      return 0;
    }
    if (univibe_jobs(self) != 0) {
      return -1;
    }
  }
  fprintf(stderr, "univibe: the level words never settled\n");
  return -1;
}

static int univibe_descriptor(gm2_firmware *self) {
  const uint32_t record = gm2_read(self->emu, GM2_X, GM2_CHAIN + UNIVIBE_ALGORITHM);
  const uint32_t table = gm2_read(self->emu, GM2_P, record + 7u);
  const uint32_t count = gm2_read(self->emu, GM2_P, record + 1u);
  if (record == 0u || table == 0u || count < UNIVIBE_OUT_LEVEL_INDEX + 1u) {
    fprintf(stderr, "univibe: descriptor X:0x%06X of P:0x%06X has %u parameters\n", record, table,
            count);
    return -1;
  }
  static const unsigned k_index[11] = {
      1u, 2u, 3u, 4u, 5u, 8u, 9u, 10u, 12u, 13u, UNIVIBE_OUT_LEVEL_INDEX};
  for (unsigned knob = 0; knob < self->effect->knobs; knob++) {
    const gm2_knob *want = &self->effect->knob[knob];
    uint32_t callback = 0;
    if (gm2_firmware_knob_entry(self->emu, table, k_index[knob], want, &callback) != 0) {
      return -1;
    }
    const int32_t entry = gm2_sign24(gm2_read(
        self->emu, GM2_P, table + UNIVIBE_ENTRY_STRIDE * k_index[knob] + UNIVIBE_ENTRY_DEFAULT));
    if (entry >= want->min && entry <= want->max && entry != want->def) {
      fprintf(stderr, "univibe: %s defaults to %d in the descriptor, %d in the knob table\n",
              want->name, (int)entry, want->def);
      return -1;
    }
  }
  return 0;
}

static int univibe_prepare(gm2_firmware *self) {
  gm2_emu *emu = self->emu;
  univibe_audio_begin(emu);
  if (gm2_mod_subtype(emu, self->block->subtype) != 0) {
    return -1;
  }
  gm2_set_reg(emu, DSP56300_REG_A2, 0);
  gm2_set_reg(emu, DSP56300_REG_A1, 0);
  gm2_set_reg(emu, DSP56300_REG_A0, 0);
  if (gm2_firmware_call(emu, UNIVIBE_SINE, GM2_FIRMWARE_CALL_BUDGET) != 0) {
    fprintf(stderr, "univibe: the sine generator P:0x%06X did not return\n", UNIVIBE_SINE);
    return -1;
  }
  univibe_raws(self);
  gm2_write(emu, GM2_X, UNIVIBE_TAP_SOURCE, 1u);
  gm2_write(emu, GM2_X, UNIVIBE_BEAT, GM2_EFFECT_BEAT_MS);
  gm2_write(emu, GM2_X, UNIVIBE_UI + 32u, UNIVIBE_INPUT);
  gm2_write(emu, GM2_X, UNIVIBE_UI + 33u, UNIVIBE_OUTPUT);
  gm2_write(emu, GM2_X, UNIVIBE_UI + 34u, UNIVIBE_LINK);
  gm2_firmware_link(emu, UNIVIBE_LINK, UNIVIBE_OUTPUT);
  for (unsigned pass = 0; pass < 2u; pass++) {
    gm2_write(emu, GM2_X, UNIVIBE_UI + 1u, pass == 0u ? 1u : 0u);
    gm2_write(emu, GM2_X, UNIVIBE_UI + 3u, 0u);
    gm2_set_reg(emu, DSP56300_REG_A2, 0);
    gm2_set_reg(emu, DSP56300_REG_A1, UNIVIBE_ALGORITHM);
    gm2_set_reg(emu, DSP56300_REG_A0, 0);
    gm2_set_reg(emu, DSP56300_REG_R0, UNIVIBE_UI);
    if (gm2_firmware_call(emu, UNIVIBE_ACTIVATION, UNIVIBE_ACTIVATION_BUDGET) != 0) {
      fprintf(stderr, "univibe: the activation P:0x%06X did not return\n", UNIVIBE_ACTIVATION);
      return -1;
    }
  }
  if (univibe_descriptor(self) != 0 || univibe_take_jobs(self) != 0) {
    return -1;
  }
  return univibe_settle_levels(self);
}

static void univibe_close(gm2_firmware *self) {
  if (self != NULL) {
    gm2_destroy(self->emu);
    free(self);
  }
}

static gm2_firmware *univibe_open(const gm2_effect *effect, const univibe_block *block) {
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
  if (self->emu == NULL || univibe_prepare(self) != 0) {
    univibe_close(self);
    return NULL;
  }
  self->audio_dirty = 1;
  return self;
}

static gm2_firmware *vintunivibe_open(void) {
  return univibe_open(&gm2_effect_vintunivibe, &k_blocks[0]);
}

static gm2_firmware *modunivibe_open(void) {
  return univibe_open(&gm2_effect_modunivibe, &k_blocks[1]);
}

/* The device's whole parameter edit, then the host's knob vector back in the raw
 * words a callback rewrote (P:0x09349C writes the nearest Speed of each Tempo that
 * is set).
 *
 * Five kinds of turn reach the LFO step updater P:0x09349C, and four of them leave
 * a target speed the knob values do not explain: the Speed 1, Speed 2 and Select
 * wrappers clear A, so the updater takes the Speed branch whatever the Tempo says,
 * and P:0x093637 writes a Tempo 2 value into Tempo 1 as well. Only the Tempo
 * callback's own A = 0x50 derives each step from the Tempo, so the updater runs
 * once more with that A; it writes Speed 1 and Speed 2 back in turn, hence the
 * second restore. */
static int univibe_set_knob(gm2_firmware *self, unsigned knob, int value) {
  static const unsigned k_index[11] = {
      1u, 2u, 3u, 4u, 5u, 8u, 9u, 10u, 12u, 13u, UNIVIBE_OUT_LEVEL_INDEX};
  self->values[knob] = (int32_t)value;
  if (gm2_firmware_edit(self->emu, UNIVIBE_EDIT_BASE + k_index[knob], UNIVIBE_EDIT, value,
                        GM2_DEVICE_STACK) != 0) {
    fprintf(stderr, "univibe: the parameter edit P:0x%06X did not return\n", UNIVIBE_EDIT);
    return -1;
  }
  univibe_raws(self);
  gm2_write(self->emu, GM2_X, UNIVIBE_TAP_SOURCE, 1u);
  gm2_write(self->emu, GM2_X, UNIVIBE_BEAT, GM2_EFFECT_BEAT_MS);
  if (knob == UNIVIBE_KNOB_SPEED_1 || knob == UNIVIBE_KNOB_SPEED_2 ||
      knob == UNIVIBE_KNOB_TEMPO_2 || knob == UNIVIBE_KNOB_SELECT) {
    gm2_set_reg(self->emu, DSP56300_REG_A2, 0);
    gm2_set_reg(self->emu, DSP56300_REG_A1, UNIVIBE_TEMPO_ARG);
    gm2_set_reg(self->emu, DSP56300_REG_A0, 0);
    gm2_set_reg(self->emu, DSP56300_REG_R0, UNIVIBE_UI);
    if (gm2_firmware_call(self->emu, UNIVIBE_LFO_STEP, UNIVIBE_ACTIVATION_BUDGET) != 0) {
      fprintf(stderr, "univibe: the LFO step P:0x%06X did not return\n", UNIVIBE_LFO_STEP);
      return -1;
    }
    univibe_raws(self);
  }
  self->audio_dirty = 1;
  return univibe_check_jobs(self);
}

/* What the port does with each of the thirty-five staged words: the twenty-one it
 * holds, in the order it holds them, and the ones it keeps as constants in its
 * kernel, with the value each must have at every setting. */
#define UNIVIBE_STAGED_WORDS 35u
#define UNIVIBE_HELD 0x1000000 /* not a 24-bit value: the word is one the port holds */

static const int32_t k_univibe_staged[UNIVIBE_STAGED_WORDS] = {
    [0] = UNIVIBE_HELD,
    [1] = 0x400000,
    [2] = UNIVIBE_HELD,
    [3] = UNIVIBE_HELD,
    [4] = UNIVIBE_HELD,
    [5] = UNIVIBE_HELD,
    [6] = UNIVIBE_HELD,
    [7] = UNIVIBE_HELD,
    [8] = UNIVIBE_HELD,
    [9] = UNIVIBE_HELD,
    [10] = 0,
    [11] = 2,
    [12] = 0x200000,
    [13] = 0x16A77E,
    [14] = 0x295882,
    [15] = 0x6D9196,
    [16] = 2,
    [17] = 2,
    [18] = 0x200000,
    [19] = 0x1C8521,
    [20] = 0x237ADF,
    [21] = 0x499058,
    [22] = 4,
    [23] = UNIVIBE_HELD,
    [24] = UNIVIBE_HELD,
    [25] = UNIVIBE_HELD,
    [26] = UNIVIBE_HELD,
    [27] = UNIVIBE_HELD,
    [28] = UNIVIBE_HELD,
    [29] = UNIVIBE_HELD,
    [30] = UNIVIBE_HELD,
    [31] = UNIVIBE_HELD,
    [32] = UNIVIBE_HELD,
    [33] = UNIVIBE_HELD,
    [34] = UNIVIBE_HELD,
};

/* The twenty-one staged words the port holds, the LFO speed's target Y:0x046C and
 * the waveshaper's two weights Y:0x0474/0x0475. */
// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int univibe_words(gm2_firmware *self, int32_t *words) {
  const uint32_t staged = univibe_staged(self->emu);
  unsigned held = 0;
  for (unsigned word = 0; word < UNIVIBE_STAGED_WORDS; word++) {
    const int32_t value = gm2_sign24(gm2_read(self->emu, GM2_X, staged + word));
    if (k_univibe_staged[word] == UNIVIBE_HELD) {
      words[held++] = value;
    } else if (value != k_univibe_staged[word]) {
      fprintf(stderr, "%s: the staged word S+%u is %06X, not the constant %06X\n",
              self->effect->name, word, (uint32_t)value & 0xFFFFFFu,
              (uint32_t)k_univibe_staged[word] & 0xFFFFFFu);
      return -1;
    }
  }
  words[held++] = gm2_sign24(gm2_read(self->emu, GM2_Y, UNIVIBE_SPEED_WORD));
  const int32_t poly_0 = gm2_sign24(gm2_read(self->emu, GM2_Y, UNIVIBE_POLY));
  const int32_t poly_3 = gm2_sign24(gm2_read(self->emu, GM2_Y, UNIVIBE_POLY + 3u));
  if (poly_0 != 0 || poly_3 != 0) {
    fprintf(stderr, "%s: the waveshaper words Y:0x%06X and Y:0x%06X are %06X and %06X, not zero\n",
            self->effect->name, UNIVIBE_POLY, UNIVIBE_POLY + 3u, (uint32_t)poly_0 & 0xFFFFFFu,
            (uint32_t)poly_3 & 0xFFFFFFu);
    return -1;
  }
  words[held++] = gm2_sign24(gm2_read(self->emu, GM2_Y, UNIVIBE_POLY + 1u));
  words[held] = gm2_sign24(gm2_read(self->emu, GM2_Y, UNIVIBE_POLY + 2u));
  return 0;
}

/* The block's own records and nothing else, after every knob turn as after the
 * activation: a turn that adds a record would leave it unrun. */
static int univibe_check_jobs(const gm2_firmware *self) {
  return gm2_firmware_check_same_jobs(self->emu, &k_jobs, self->record, self->records);
}

/* The records the block holds after its activation, kept so every chunk runs the
 * firmware's own list and not a list of this adapter's making. The two a knob turn
 * moves must be in it: the Depth pair's linear record P:0x09339D arms, and the level
 * code's record. */
static int univibe_take_jobs(gm2_firmware *self) {
  const uint32_t staged = univibe_staged(self->emu);
  uint32_t depth = 0;
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
    if (params == UNIVIBE_LEVEL_PARAMS) {
      level += handler == GM2_JOB_SMOOTH && source == UNIVIBE_LEVEL_STAGED &&
                       target == UNIVIBE_LEVEL && count == UNIVIBE_LEVEL_WORDS
                   ? 1u
                   : 0u;
      continue;
    }
    if (handler == GM2_JOB_LINEAR && source == staged && target == UNIVIBE_LIVE_DEPTH &&
        count == 2u) {
      depth++;
    }
  }
  if (depth != 1u || level != 1u) {
    fprintf(stderr, "univibe: the block's records do not move its depth pair and level words\n");
    return -1;
  }
  return 0;
}

/* Each record {previous, one, its payload, its handler}, run the way P:0x0001CD
 * runs it: R0 on the payload, then the handler. */
static int univibe_jobs(gm2_firmware *self) {
  return gm2_firmware_run_list(self->emu, self->record, self->records);
}

static int univibe_frame(gm2_firmware *self, int32_t left, int32_t right, int32_t *out_left,
                         int32_t *out_right) {
  gm2_emu *emu = self->emu;
  if (self->audio_dirty != 0) {
    univibe_audio_begin(emu);
    self->audio_dirty = 0;
  }
  gm2_write(emu, GM2_X, UNIVIBE_INPUT, gm2_word24(left));
  gm2_write(emu, GM2_X, UNIVIBE_INPUT + 1u, gm2_word24(right));
  gm2_write(emu, GM2_X, UNIVIBE_CHUNK_WORD, UNIVIBE_CHUNK_FRAMES - self->chunk_pos);
  gm2_set_reg(emu, DSP56300_REG_R6, UNIVIBE_TASK + 1u);
  gm2_set_reg(emu, DSP56300_REG_M6, 0xFFFFFFu);
  if (gm2_run_until_stop(emu, UNIVIBE_KERNEL, GM2_FIRMWARE_CALL_BUDGET, NULL) != GM2_CALL_OK) {
    return -1;
  }
  *out_left = gm2_sign24(gm2_read(emu, GM2_X, UNIVIBE_OUTPUT));
  *out_right = gm2_sign24(gm2_read(emu, GM2_X, UNIVIBE_OUTPUT + 1u));
  self->chunk_pos++;
  if (self->chunk_pos == UNIVIBE_CHUNK_FRAMES) {
    self->chunk_pos = 0;
    return univibe_jobs(self);
  }
  return 0;
}

const gm2_firmware_ops gm2_firmware_vintunivibe = {
    .effect = &gm2_effect_vintunivibe,
    .open = vintunivibe_open,
    .levels = univibe_levels,
    .set_knob = univibe_set_knob,
    .words = univibe_words,
    .frame = univibe_frame,
    .close = univibe_close,
};

const gm2_firmware_ops gm2_firmware_modunivibe = {
    .effect = &gm2_effect_modunivibe,
    .open = modunivibe_open,
    .levels = univibe_levels,
    .set_knob = univibe_set_knob,
    .words = univibe_words,
    .frame = univibe_frame,
    .close = univibe_close,
};
