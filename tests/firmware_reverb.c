/* The Reverb block as the firmware runs it: the subtype's factory bank that
 * P:0x0951AD saves at UI+3..58, the activation P:0x095099 with the level code it
 * ends on, the parameter edit P:0x083268 for a knob turn, the job records the
 * activation and the level code queue, and the audio continuation P:0x0012D2 with
 * every record of the list run after every four-frame chunk. Subtype is a
 * parameter (X:0x099B86), not a knob, so each effect fixes it and takes the bank
 * its type names.
 */
#include "firmware_common.h"
#include "gm2_effect.h"
#include "gm2_emu.h"
#include "gm2_job.h"
#include "gm2_record.h"
#include "gm2_reverb.h"

#include <dsp56300.h>

#include <stdio.h>
#include <stdlib.h>

#define REVERB_BUFFER 0x0A0000u /* Y: the 32,768-word buffer */
#define REVERB_ALGORITHM 5u
#define REVERB_UI 0x098FF4u
#define REVERB_BANKS (REVERB_UI + 3u) /* the four fourteen-word subtype banks */
#define REVERB_BANK_WORDS 14u
#define REVERB_PARAMS 0x099B86u /* Subtype, the eleven knobs, Mix, OutLevel */
#define REVERB_FIRST_SLOT 1u
#define REVERB_MIX_SLOT 12u
#define REVERB_OUT_SLOT 13u
#define REVERB_TAP_TIME 0x099A6Bu
#define REVERB_INIT_ENTRY 0x0951ADu
#define REVERB_ACTIVATION 0x095099u
#define REVERB_LEVEL_ENTRY 0x095BE1u
#define REVERB_EDIT 0x083268u  /* the device's parameter edit: A1 = row + index, B1 = value */
#define REVERB_EDIT_BASE 0x50u /* the row A1 carries: 0x099B36 + it is REVERB_PARAMS */
#define REVERB_LEVEL_PARAMS 0x099040u /* X: the level words' job record */
#define REVERB_LEVEL_STAGED 0x099046u
#define REVERB_LEVEL 0x05DAu /* Y: the input, wet and dry words the level job moves */
#define REVERB_LEVEL_WORDS 3u
#define REVERB_STAGING_OBJECT 85u /* UI+85: staged words, then the state and task bases */
#define REVERB_STAGED 0x098F64u
#define REVERB_STATE 0x037Du
#define REVERB_TASK 0x057Bu
#define REVERB_LIVE 0x0589u /* the record's +7 word: Y+14, the live words */
#define REVERB_ZERO_WORD (REVERB_LIVE + 49u) /* Y+63, between the tank and comb words */
#define REVERB_FIRST_JOB 0x098FB4u /* X: the first of the six parameter records */
#define REVERB_KERNEL 0x0012D2u
#define REVERB_INPUT 0x0600u
#define REVERB_OUTPUT 0x0620u
#define REVERB_LINK 0x0610u
#define REVERB_SETUP_WORDS                                                                         \
  (GM2_REVERB_COEFS + GM2_REVERB_OFFSETS + GM2_REVERB_TANK_SIGNS + 2u * GM2_REVERB_EARLY_TAPS)
#define REVERB_FRAME_BUDGET 100000
#define REVERB_SETTLE_ROUNDS 20000u

/* The six groups of smoothed words: the parameter record the runner puts on R0 and
 * the routine it runs, both as the activation queues them, and the live words each
 * takes from the staged ones: REVERB_LIVE plus `live` from X:0x098F64 plus
 * `staged`. The two biquad groups run the two-row variant P:0x00026F.
 * REVERB_ZERO_WORD is a zero word no job writes. */
static const struct {
  uint32_t params, entry;
  unsigned live, staged, count;
} k_groups[6] = {
    {REVERB_FIRST_JOB, GM2_JOB_SMOOTH, 0u, 0u, 49u},
    {REVERB_FIRST_JOB + 6u, GM2_JOB_SMOOTH, 50u, 49u, 3u},
    {REVERB_FIRST_JOB + 12u, GM2_JOB_SMOOTH, 63u, 52u, 3u},
    {REVERB_FIRST_JOB + 18u, GM2_JOB_GROUP, 53u, 55u, 10u},
    {REVERB_FIRST_JOB + 26u, GM2_JOB_GROUP, 66u, 65u, 10u},
    {REVERB_FIRST_JOB + 34u, GM2_JOB_SMOOTH, 76u, 75u, 5u},
};

struct gm2_firmware {
  gm2_emu *emu;
  unsigned chunk_pos;
};

/* The block's latest published level words: the input, wet and dry words the
 * kernel reads through the task record. */
// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int reverb_levels(gm2_firmware *self, int32_t level[3]) {
  const uint32_t words = gm2_read(self->emu, GM2_Y, REVERB_TASK + 2u);
  if (words != 0x05DAu) {
    fprintf(stderr, "reverb: the task record names level words at Y:0x%03X\n", words);
    return -1;
  }
  for (unsigned i = 0; i < 3u; i++) {
    level[i] = gm2_sign24(gm2_read(self->emu, GM2_Y, words + i));
  }
  return 0;
}

/* The descriptor's name, clamp window and callback for every knob, against the
 * knob table: a firmware whose table moved cannot be recorded against this effect,
 * and an entry without a callback would leave the edit P:0x083268 doing nothing. */
static int reverb_descriptor(const gm2_firmware *self, const gm2_effect *effect) {
  uint32_t record = gm2_read(self->emu, GM2_X, GM2_CHAIN + REVERB_ALGORITHM);
  uint32_t table = gm2_read(self->emu, GM2_P, record + 7u);
  uint32_t count = gm2_read(self->emu, GM2_P, record + 1u);
  if (record == 0u || count < REVERB_FIRST_SLOT + effect->knobs) {
    fprintf(stderr, "%s: descriptor X:0x%06X has %u parameters\n", effect->name, record, count);
    return -1;
  }
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    uint32_t callback = 0;
    if (gm2_firmware_knob_entry(self->emu, table, REVERB_FIRST_SLOT + knob, &effect->knob[knob],
                                &callback) != 0) {
      return -1;
    }
  }
  return 0;
}

/* The block's list holds the six smoothing groups and the level words' job and
 * nothing else, each on the group's own words: a knob turn that queues a record the
 * adapter does not run makes the recording fail. */
static int reverb_check_jobs(const gm2_firmware *self) {
  uint32_t record[GM2_FIRMWARE_JOBS_MAX];
  unsigned count = gm2_firmware_jobs(self->emu, record, GM2_FIRMWARE_JOBS_MAX);
  if (count == 0u) {
    fprintf(stderr, "reverb: cannot read the block's job records\n");
    return -1;
  }
  unsigned seen = 0u;
  for (unsigned i = 0; i < count; i++) {
    const uint32_t handler = gm2_read(self->emu, GM2_X, record[i] + GM2_JOB_HANDLER);
    const uint32_t params = gm2_read(self->emu, GM2_X, record[i] + GM2_JOB_PARAMS);
    unsigned group = 0;
    while (group < 6u && k_groups[group].params != params) {
      group++;
    }
    if (group == 6u) {
      if (params != REVERB_LEVEL_PARAMS || handler != GM2_JOB_SMOOTH ||
          gm2_read(self->emu, GM2_X, params) != REVERB_LEVEL_STAGED ||
          gm2_read(self->emu, GM2_X, params + 1u) != REVERB_LEVEL ||
          gm2_read(self->emu, GM2_X, params + 2u) != REVERB_LEVEL_WORDS ||
          gm2_read(self->emu, GM2_X, params + 3u) != 1u ||
          gm2_read(self->emu, GM2_X, params + 4u) != GM2_JOB_SMOOTH_STEP) {
        fprintf(stderr, "reverb: job X:0x%06X runs P:0x%06X on X:0x%06X, not a Reverb record\n",
                record[i], handler, params);
        return -1;
      }
      continue;
    }
    if (handler != k_groups[group].entry ||
        gm2_read(self->emu, GM2_X, params) != REVERB_STAGED + k_groups[group].staged ||
        gm2_read(self->emu, GM2_X, params + 1u) != REVERB_LIVE + k_groups[group].live ||
        gm2_read(self->emu, GM2_X, params + 2u) != k_groups[group].count ||
        gm2_read(self->emu, GM2_X, params + 3u) != 1u ||
        gm2_read(self->emu, GM2_X, params + 4u) != GM2_JOB_SMOOTH_STEP) {
      fprintf(stderr,
              "reverb: the record at X:0x%06X runs P:0x%06X, not the words the port "
              "smooths\n",
              record[i], handler);
      return -1;
    }
    seen |= 1u << group;
  }
  if (seen != 0x3Fu) {
    fprintf(stderr, "reverb: the list holds the records of groups 0x%02X\n", seen);
    return -1;
  }
  return 0;
}

/* Every record the list holds, in list order, entered with R0 on its parameter
 * record as the runner P:0x0001CD does. */
static int reverb_jobs(gm2_firmware *self) {
  return gm2_firmware_run_jobs(self->emu, "reverb", GM2_FIRMWARE_JOBS_MAX);
}

/* The device's path for a knob turn: the whole parameter edit P:0x083268, which
 * clamps the value, stores the raw word and calls the parameter's own callback.
 * Only Subtype's descriptor carries the two bits that make the edit call its
 * callback a second time, and no effect turns Subtype. The knob vector is the
 * host's, so a raw word a callback rewrote is put back without its callback. */
static int reverb_set_knob(gm2_firmware *self, unsigned knob, int value) {
  const unsigned slot = REVERB_FIRST_SLOT + knob;
  uint32_t before[REVERB_OUT_SLOT + 1u];
  for (unsigned i = 0; i <= REVERB_OUT_SLOT; i++) {
    before[i] = gm2_read(self->emu, GM2_X, REVERB_PARAMS + i);
  }
  if (gm2_firmware_edit(self->emu, REVERB_EDIT_BASE + slot, REVERB_EDIT, value, GM2_DEVICE_STACK) !=
      0) {
    fprintf(stderr, "reverb: the parameter edit P:0x%06X did not return for knob %u\n", REVERB_EDIT,
            knob);
    return -1;
  }
  for (unsigned i = 0; i <= REVERB_OUT_SLOT; i++) {
    if (i != slot && gm2_read(self->emu, GM2_X, REVERB_PARAMS + i) != before[i]) {
      gm2_write(self->emu, GM2_X, REVERB_PARAMS + i, before[i]);
    }
  }
  return reverb_check_jobs(self);
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int reverb_words(gm2_firmware *self, int32_t *words) {
  unsigned at = 0;
  for (unsigned i = 0; i < GM2_REVERB_COEFS; i++) {
    words[at++] = gm2_sign24(gm2_read(self->emu, GM2_X, REVERB_STAGED + i));
  }
  for (unsigned i = 0; i < GM2_REVERB_OFFSETS; i++) {
    words[at++] = gm2_sign24(gm2_read(self->emu, GM2_X, REVERB_STATE + 1u + i));
  }
  for (unsigned i = 0; i < GM2_REVERB_TANK_SIGNS; i++) {
    words[at++] = gm2_sign24(gm2_read(self->emu, GM2_X, REVERB_STATE + 63u + i));
  }
  for (unsigned i = 0; i < GM2_REVERB_EARLY_TAPS; i++) {
    words[at++] = gm2_sign24(gm2_read(self->emu, GM2_X, REVERB_STATE + 106u + i));
  }
  for (unsigned i = 0; i < GM2_REVERB_EARLY_TAPS; i++) {
    words[at++] = gm2_sign24(gm2_read(self->emu, GM2_X, REVERB_STATE + 116u + i));
  }
  return at == REVERB_SETUP_WORDS ? 0 : -1;
}

/* One stereo frame through the continuation, and the block's jobs once per
 * four-frame chunk. */
static int reverb_frame(gm2_firmware *self, int32_t left, int32_t right, int32_t *out_left,
                        int32_t *out_right) {
  gm2_write(self->emu, GM2_X, REVERB_INPUT, gm2_word24(left));
  gm2_write(self->emu, GM2_X, REVERB_INPUT + 1u, gm2_word24(right));
  gm2_set_reg(self->emu, DSP56300_REG_R6, REVERB_TASK + 1u);
  gm2_set_reg(self->emu, DSP56300_REG_M6, 0xFFFFFFu);
  if (gm2_run_until_stop(self->emu, REVERB_KERNEL, REVERB_FRAME_BUDGET, NULL) != GM2_CALL_OK) {
    return -1;
  }
  *out_left = gm2_sign24(gm2_read(self->emu, GM2_X, REVERB_OUTPUT));
  *out_right = gm2_sign24(gm2_read(self->emu, GM2_X, REVERB_OUTPUT + 1u));
  self->chunk_pos++;
  if (self->chunk_pos == GM2_CHUNK_FRAMES) {
    self->chunk_pos = 0;
    return reverb_jobs(self);
  }
  return 0;
}

static void reverb_close(gm2_firmware *self) {
  if (self != NULL) {
    gm2_destroy(self->emu);
    free(self);
  }
}

/* The rest state the native open() starts in: every group on its staged word and
 * the three level words glided up from their power-on zero by the block's own
 * level job. */
static int reverb_settle(gm2_firmware *self) {
  for (unsigned round = 0; round < REVERB_SETTLE_ROUNDS; round++) {
    if (reverb_jobs(self) != 0) {
      return -1;
    }
    unsigned same = 0;
    for (unsigned group = 0; group < 6u; group++) {
      for (unsigned k = 0; k < k_groups[group].count; k++) {
        same += gm2_read(self->emu, GM2_Y, REVERB_LIVE + k_groups[group].live + k) ==
                gm2_read(self->emu, GM2_X, REVERB_STAGED + k_groups[group].staged + k);
      }
    }
    for (unsigned word = 0; word < REVERB_LEVEL_WORDS; word++) {
      same += gm2_read(self->emu, GM2_Y, REVERB_LEVEL + word) ==
              gm2_read(self->emu, GM2_X, REVERB_LEVEL_STAGED + word);
    }
    if (same == GM2_REVERB_COEFS + REVERB_LEVEL_WORDS) {
      return 0;
    }
  }
  fprintf(stderr, "reverb: the staged words never settled\n");
  return -1;
}

/* A fresh block running subtype `type`: the factory bank of that subtype at the
 * raw slots, with Mix at 100 % and Out Level 0 dB until the bank's own words
 * replace them, the activation, the Out Level callback, the settled rest state and
 * the harness's audio wiring. */
static gm2_firmware *reverb_open(unsigned type, const gm2_effect *effect) {
  gm2_firmware *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->emu = gm2_firmware_boot(effect->name);
  if (self->emu == NULL || gm2_mirror_descriptor(self->emu, REVERB_ALGORITHM) < 0 ||
      reverb_descriptor(self, effect) != 0) {
    reverb_close(self);
    return NULL;
  }
  gm2_write(self->emu, GM2_X, REVERB_PARAMS, type);
  gm2_write(self->emu, GM2_X, REVERB_PARAMS + REVERB_MIX_SLOT, 100u);
  gm2_write(self->emu, GM2_X, REVERB_TAP_TIME, GM2_EFFECT_BEAT_MS);

  gm2_set_reg(self->emu, DSP56300_REG_R0, REVERB_UI);
  gm2_set_reg(self->emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  if (gm2_call(self->emu, REVERB_INIT_ENTRY, GM2_FIRMWARE_CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "%s: the factory bank routine P:0x%06X did not return\n", effect->name,
            REVERB_INIT_ENTRY);
    reverb_close(self);
    return NULL;
  }
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    uint32_t bank = gm2_read(self->emu, GM2_X, REVERB_BANKS + REVERB_BANK_WORDS * type + knob);
    int32_t value = gm2_sign24(bank);
    if (value != effect->knob[knob].def) {
      fprintf(stderr, "%s: the factory bank holds %s = %d, the knob table %d\n", effect->name,
              effect->knob[knob].name, (int)value, effect->knob[knob].def);
      reverb_close(self);
      return NULL;
    }
    gm2_write(self->emu, GM2_X, REVERB_PARAMS + REVERB_FIRST_SLOT + knob, bank);
  }

  gm2_write(self->emu, GM2_X, REVERB_UI + 1u, 1u);
  gm2_set_reg(self->emu, DSP56300_REG_A2, 0);
  gm2_set_reg(self->emu, DSP56300_REG_A1, REVERB_ALGORITHM);
  gm2_set_reg(self->emu, DSP56300_REG_A0, 0);
  gm2_set_reg(self->emu, DSP56300_REG_R0, REVERB_UI);
  gm2_set_reg(self->emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  if (gm2_call(self->emu, REVERB_ACTIVATION, GM2_FIRMWARE_CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "%s: the activation P:0x%06X did not return\n", effect->name,
            REVERB_ACTIVATION);
    reverb_close(self);
    return NULL;
  }
  if (gm2_read(self->emu, GM2_X, REVERB_UI + REVERB_STAGING_OBJECT) != REVERB_STAGED ||
      gm2_read(self->emu, GM2_X, REVERB_UI + REVERB_STAGING_OBJECT + 1u) != REVERB_STATE ||
      gm2_read(self->emu, GM2_X, REVERB_UI + REVERB_STAGING_OBJECT + 2u) != REVERB_TASK) {
    fprintf(stderr, "%s: the block keeps its words at %06X/%06X/%06X\n", effect->name,
            gm2_read(self->emu, GM2_X, REVERB_UI + REVERB_STAGING_OBJECT),
            gm2_read(self->emu, GM2_X, REVERB_UI + REVERB_STAGING_OBJECT + 1u),
            gm2_read(self->emu, GM2_X, REVERB_UI + REVERB_STAGING_OBJECT + 2u));
    reverb_close(self);
    return NULL;
  }

  gm2_set_reg(self->emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  if (gm2_call(self->emu, REVERB_LEVEL_ENTRY, GM2_FIRMWARE_CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "%s: the level code P:0x%06X did not return\n", effect->name,
            REVERB_LEVEL_ENTRY);
    reverb_close(self);
    return NULL;
  }

  /* The block's list: the six smoothing groups the activation queues and the level
   * job the level code queues. */
  if (reverb_check_jobs(self) != 0) {
    reverb_close(self);
    return NULL;
  }

  /* The three words the port fixes from the init table: the continuation it
   * translates, the base of the live words and the zero its last tank read lands
   * on. */
  if (gm2_read(self->emu, GM2_Y, REVERB_TASK) != REVERB_KERNEL ||
      gm2_read(self->emu, GM2_Y, REVERB_TASK + 7u) != REVERB_LIVE ||
      gm2_read(self->emu, GM2_Y, REVERB_ZERO_WORD) != 0u) {
    fprintf(
        stderr, "%s: the record names P:0x%06X, the live words at Y:0x%03X, the zero word %06X\n",
        effect->name, gm2_read(self->emu, GM2_Y, REVERB_TASK),
        gm2_read(self->emu, GM2_Y, REVERB_TASK + 7u), gm2_read(self->emu, GM2_Y, REVERB_ZERO_WORD));
    reverb_close(self);
    return NULL;
  }

  /* The record's audio wiring: the harness's input and output buffers, and the
   * stop link the continuation jumps through instead of the next task. */
  const uint32_t record[2] = {GM2_STUB_ADDR + 2u, REVERB_OUTPUT};
  gm2_write_run(self->emu, GM2_Y, REVERB_LINK, record, 2);
  gm2_write(self->emu, GM2_Y, REVERB_TASK + 1u, REVERB_INPUT);
  gm2_write(self->emu, GM2_Y, REVERB_TASK + 11u, REVERB_INPUT);
  gm2_write(self->emu, GM2_Y, REVERB_TASK + 12u, REVERB_OUTPUT);
  gm2_write(self->emu, GM2_Y, REVERB_TASK + 13u, REVERB_LINK);

  /* At rest: the words a long silence leaves, as the native open() starts. */
  if (reverb_settle(self) != 0) {
    reverb_close(self);
    return NULL;
  }
  /* An empty circular buffer. */
  for (unsigned i = 0; i < GM2_REVERB_BUFFER_WORDS; i++) {
    if (gm2_read(self->emu, GM2_Y, REVERB_BUFFER + i) != 0u) {
      fprintf(stderr, "%s: the circular buffer holds %06X at Y:0x%05X\n", effect->name,
              gm2_read(self->emu, GM2_Y, REVERB_BUFFER + i), REVERB_BUFFER + i);
      reverb_close(self);
      return NULL;
    }
  }
  return self;
}

#define REVERB_OPEN(type, name)                                                                    \
  static gm2_firmware *name##_open(void) { return reverb_open(type, &gm2_effect_##name); }

REVERB_OPEN(0u, reverb_spring)
REVERB_OPEN(1u, reverb_hall)
REVERB_OPEN(2u, reverb_room)
REVERB_OPEN(3u, reverb_plate)

#define REVERB_OPS(name)                                                                           \
  const gm2_firmware_ops gm2_firmware_##name = {                                                   \
      .effect = &gm2_effect_##name,                                                                \
      .open = name##_open,                                                                         \
      .levels = reverb_levels,                                                                     \
      .set_knob = reverb_set_knob,                                                                 \
      .words = reverb_words,                                                                       \
      .frame = reverb_frame,                                                                       \
      .close = reverb_close,                                                                       \
  };

REVERB_OPS(reverb_spring)
REVERB_OPS(reverb_hall)
REVERB_OPS(reverb_room)
REVERB_OPS(reverb_plate)
