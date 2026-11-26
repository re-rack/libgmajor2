/* The Filter block's two resonance filters as the firmware runs them: the
 * activation P:0x091BB1 with its coefficient design, the parameter edit
 * P:0x083268, the audio continuation P:0x000EE9 with its task record Y:0x3C4, the
 * coefficient job X:0x09893C and the level record X:0x098987, and for the Touch Res
 * Filter the Noise Gate's detector behind the tick P:0x091CF9.
 */
#include "firmware_common.h"
#include "gm2_effect.h"
#include "gm2_emu.h"
#include "gm2_record.h"
#include "gm2_res_core.h"
#include "gm2_resfilter.h"
#include "gm2_tick.h"
#include "gm2_touchres.h"

#include <dsp56300.h>

#include <stdio.h>
#include <stdlib.h>

#define RES_ALGORITHM 1u
#define RES_UI 0x098948u
#define RES_RAW 0x099B46u
#define RES_RAW_WORDS 16u
#define RES_SUBTYPE_INDEX 0u
#define RES_MIX_INDEX 13u
#define RES_OUT_LEVEL_INDEX 14u
/* The descriptor's format word at entry +12: with bits 23 and 13 set, the edit
 * P:0x083268 calls the callback a second time, A = 0x50 and the value at
 * X:0x09860C. No Resonance Filter or Touch Res Filter knob has both bits. */
#define RES_DESCRIPTOR_STRIDE 15u
#define RES_DESCRIPTOR_FORMAT 12u
#define RES_FORMAT_SECOND 0x802000u
#define RES_SECOND_ARG 0x50u
/* X:0x09860C, the value the edit's second call hands a callback. */
#define RES_EDIT_VALUE 0x09860Cu
/* The raw indices the two filters' descriptors use. */
#define RES_STYLE 1u
#define RES_RESONANCE 12u
#define RES_ACTIVATION 0x091BB1u
/* P:0x091F81: the two constant pairs Style and Res set, which the tick designs
 * with. The activation and the Resonance Filter's Resonance callback run it; the
 * Touch Res Filter has no Resonance knob, so its own edit path does not. */
#define RES_STYLE_CONSTANTS 0x091F81u
#define RES_TICK_ENTRY 0x091CF9u
#define RES_LEVEL_WORD 0x0B03u
#define RES_AUDIO_ENTRY 0x000EE9u
#define RES_TASK 0x03C4u
#define RES_INPUT 0x0600u
#define RES_OUTPUT 0x0620u
#define RES_LINK_RECORD 0x070Bu
#define RES_INPUT_POINTER 0x03C5u
#define RES_DRY_POINTER 0x03CCu
#define RES_OUTPUT_POINTER 0x03CDu
#define RES_LINK_POINTER 0x03CEu
#define RES_CHUNK_WORD 0x0001u
#define RES_JOB 0x09893Cu
#define RES_JOB_LEVEL 0x09898Bu
#define RES_JOBS_MAX 8u
#define RES_STAGED 0x098928u
#define RES_LIVE 0x03DAu
#define RES_LEVEL_STAGED 0x098991u
/* P:0x08F763 and the level struct at UI+54 it takes in R0, which the recorders of
 * P:0x091B3A hold as the absolute address 0x09897E. The Mix callback P:0x09220C
 * reaches it only when the block's gate word X:(UI+3) is clear, which neither
 * activation pass writes. */
#define RES_LEVEL_CODE 0x08F763u
#define RES_LEVEL_STRUCT (RES_UI + 54u)
#define RES_LEVEL 0x03EEu
#define RES_STATE 0x0060u
#define RES_STATE_WORDS 12u
/* The Touch Res Filter's tick words, each pair exponent word first. */
#define RES_UI_COEFF (RES_UI + 32u)
#define RES_UI_THRESHOLD (RES_UI + 34u)
#define RES_UI_CENTRE (RES_UI + 38u)
#define RES_UI_SPAN (RES_UI + 40u)
#define RES_UI_Q (RES_UI + 42u)
#define RES_UI_SCALE (RES_UI + 44u)
#define RES_TICK_PAIRS 6u
/* The Noise Gate block, run on the same input for its peak hold alone. */
#define RES_GATE_ALGORITHM 6u
#define RES_GATE_UI 0x0987A4u
#define RES_GATE_ACTIVATION 0x09038Bu
#define RES_GATE_ENTRY 0x000C47u
#define RES_GATE_TASK 0x0360u
#define RES_GATE_LINK_RECORD 0x0700u
#define RES_GATE_OUTPUT 0x0640u
#define RES_GATE_STATE 0x02A0u
#define RES_GATE_STATE_WORDS 5u
#define RES_GATE_CONFIG 0x0371u
#define RES_GATE_PEAK 0x0377u
#define RES_GATE_LEVEL 0x0394u
#define RES_GATE_LINK 0x090441u
#define RES_GATE_READ 0x090494u
#define RES_BUDGET 400000
#define RES_SETTLE_LIMIT 400000
#define RES_KNOB_MAX 6u

struct gm2_firmware {
  gm2_emu *emu;
  const gm2_effect *effect;
  unsigned subtype;
  unsigned knobs;
  unsigned descriptor[RES_KNOB_MAX];
  uint32_t callback[RES_KNOB_MAX];
  uint32_t format[RES_KNOB_MAX];
  int32_t raw[RES_RAW_WORDS];
  unsigned tick_chunks;
  unsigned chunks;
  unsigned frame;
  int audio_dirty;
};

static const unsigned k_resfilter_descriptor[] = {1u, 11u, 12u, 13u, RES_OUT_LEVEL_INDEX};
static const unsigned k_touchres_descriptor[] = {1u, 2u, 3u, 6u, 13u, RES_OUT_LEVEL_INDEX};
static const uint32_t k_touchres_pairs[RES_TICK_PAIRS] = {
    RES_UI_COEFF, RES_UI_THRESHOLD, RES_UI_CENTRE, RES_UI_SPAN, RES_UI_Q, RES_UI_SCALE,
};

/* A: the block index the activation reads; the updaters, the job and the level
 * code take their words from R0 alone. */
static int res_call(gm2_firmware *self, uint32_t target, uint32_t a, uint32_t r0, int32_t budget) {
  gm2_emu *emu = self->emu;
  gm2_set_reg(emu, DSP56300_REG_A2, 0);
  gm2_set_reg(emu, DSP56300_REG_A1, a);
  gm2_set_reg(emu, DSP56300_REG_A0, 0);
  gm2_set_reg(emu, DSP56300_REG_R0, r0);
  gm2_set_reg(emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  if (gm2_call(emu, target, budget, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "%s: P:0x%06X did not return\n", self->effect->name, target);
    return -1;
  }
  return 0;
}

/* The gate's constants, its level words from its own activation, and a task record
 * whose output is scratch: only its peak hold matters here. */
static int res_gate(gm2_firmware *self) {
  gm2_emu *emu = self->emu;
  uint32_t level_addr = 0;
  uint32_t levels[3];
  if (gm2_block_levels(GM2_FIRMWARE_DATA_DIR, GM2_EFFECT_RATE, RES_GATE_ALGORITHM, RES_GATE_UI,
                       RES_GATE_ACTIVATION, &level_addr, levels) != 0) {
    return -1;
  }
  if (level_addr != RES_GATE_LEVEL) {
    fprintf(stderr, "%s: the gate's level code targets Y:0x%06X, not Y:0x%06X\n",
            self->effect->name, level_addr, RES_GATE_LEVEL);
    return -1;
  }
  gm2_write(emu, GM2_X, RES_GATE_UI, RES_GATE_ALGORITHM);
  gm2_write(emu, GM2_X, RES_GATE_UI + 4u, 1u);
  gm2_write(emu, GM2_X, RES_GATE_UI + 9u, 0u);
  if (gm2_mirror_descriptor(emu, RES_GATE_ALGORITHM) < 0) {
    return -1;
  }
  if (res_call(self, RES_GATE_LINK, 0u, RES_GATE_UI, RES_BUDGET) != 0) {
    return -1;
  }
  gm2_write_run(emu, GM2_Y, RES_GATE_LEVEL, levels, 3);
  for (unsigned i = 0; i < RES_GATE_STATE_WORDS; i++) {
    gm2_write(emu, GM2_X, RES_GATE_STATE + i, 0);
  }
  gm2_write(emu, GM2_Y, RES_GATE_CONFIG, 0);
  gm2_write(emu, GM2_Y, RES_GATE_CONFIG + 1u, 0);
  gm2_write(emu, GM2_Y, RES_GATE_PEAK, 0x800000u);
  const uint32_t record[7] = {RES_INPUT,           RES_GATE_LEVEL,      RES_GATE_STATE,
                              RES_GATE_CONFIG,     RES_GATE_LEVEL + 1u, RES_GATE_OUTPUT,
                              RES_GATE_LINK_RECORD};
  gm2_write_run(emu, GM2_Y, RES_GATE_TASK, record, 7);
  const uint32_t link[2] = {GM2_STUB_ADDR + 2u, RES_GATE_OUTPUT};
  gm2_write_run(emu, GM2_Y, RES_GATE_LINK_RECORD, link, 2);
  return 0;
}

static int res_select(gm2_firmware *self) {
  if (gm2_filter_subtype(self->emu, self->subtype) != 0) {
    return -1;
  }
  uint32_t record = gm2_read(self->emu, GM2_X, GM2_CHAIN + 1u);
  if (record == 0u || gm2_read(self->emu, GM2_X, record + 7u) == 0u) {
    fprintf(stderr, "%s: subtype %u has no descriptor\n", self->effect->name, self->subtype);
    return -1;
  }
  return 0;
}

static int res_descriptor(gm2_firmware *self) {
  uint32_t record = gm2_read(self->emu, GM2_X, GM2_CHAIN + 1u);
  uint32_t table = gm2_read(self->emu, GM2_X, record + 7u);
  unsigned count = gm2_read(self->emu, GM2_X, record + 1u);
  for (unsigned knob = 0; knob < self->knobs; knob++) {
    if (self->descriptor[knob] >= count) {
      fprintf(stderr, "%s: the descriptor holds %u parameters, %s is %u\n", self->effect->name,
              count, self->effect->knob[knob].name, self->descriptor[knob]);
      return -1;
    }
    if (gm2_firmware_knob_entry(self->emu, table, self->descriptor[knob], &self->effect->knob[knob],
                                &self->callback[knob]) != 0) {
      return -1;
    }
    self->format[knob] =
        gm2_read(self->emu, GM2_P,
                 table + RES_DESCRIPTOR_STRIDE * self->descriptor[knob] + RES_DESCRIPTOR_FORMAT);
  }
  return 0;
}

/* One callback with the registers the device's edit path P:0x083268 hands it: the
 * algorithm in A, 0x50 for the second call, the block's raw array in R0 and the
 * value at X:0x09860C. Every callback of these two subtypes takes R0, and the
 * frequency A and B, from its own wrapper. */
static int res_apply(gm2_firmware *self, uint32_t callback, uint32_t a, uint32_t value) {
  gm2_write(self->emu, GM2_X, RES_EDIT_VALUE, value & 0xFFFFFFu);
  return res_call(self, callback, a, RES_RAW, RES_BUDGET);
}

/* The callback of the knob whose descriptor index is `index`, or 0. */
static uint32_t res_callback(const gm2_firmware *self, unsigned index) {
  for (unsigned knob = 0; knob < self->knobs; knob++) {
    if (self->descriptor[knob] == index) {
      return self->callback[knob];
    }
  }
  return 0;
}

/* The knob vector is the host's: a word a callback rewrote goes back without its
 * own callback. */
static void res_restore(gm2_firmware *self) {
  for (unsigned knob = 0; knob < self->knobs; knob++) {
    const unsigned slot = self->descriptor[knob];
    const uint32_t want = (uint32_t)self->raw[slot] & 0xFFFFFFu;
    if (gm2_read(self->emu, GM2_X, RES_RAW + slot) != want) {
      gm2_write(self->emu, GM2_X, RES_RAW + slot, want);
    }
  }
}

/* The rebuilds a turn needs: a Mix or Out Level turn's level code, the one the
 * callbacks P:0x09220C and P:0x0921A1 run themselves once the block's gate word
 * X:(UI+3) is clear, which neither activation pass writes; and the constants a
 * Style turn needs, because
 * neither Style callback refreshes the ones P:0x091F81 sets. The Resonance
 * Filter's Style callback P:0x0921D3 designs with the previous Style's pair, so
 * its Resonance callback P:0x0921F7 -- the constants, then the design -- is what
 * puts the vector's design back; the Touch Res Filter has no Resonance knob, and
 * its tick P:0x091CF9 takes the Style's raw word for the biquad set alone, so the
 * constants routine alone is what it needs.
 */
static int res_rebuild(gm2_firmware *self, unsigned knob) {
  if (self->descriptor[knob] == RES_MIX_INDEX || self->descriptor[knob] == RES_OUT_LEVEL_INDEX) {
    return res_call(self, RES_LEVEL_CODE, 0u, RES_LEVEL_STRUCT, RES_BUDGET);
  }
  if (self->descriptor[knob] != RES_STYLE) {
    return 0;
  }
  if (self->subtype == 4u) {
    return res_call(self, RES_STYLE_CONSTANTS, 0u, RES_UI, RES_BUDGET);
  }
  const uint32_t callback = res_callback(self, RES_RESONANCE);
  if (callback == 0u) {
    fprintf(stderr, "%s: no Resonance knob to rebuild from\n", self->effect->name);
    return -1;
  }
  return res_call(self, callback, 0u, RES_UI, RES_BUDGET);
}

static int res_check_jobs(const gm2_firmware *self);

/* The device's parameter edit P:0x083268 for one turn: the raw word, the entry's
 * callback, the second call when the format word asks for it, then the rebuild. The
 * edit returns before the callback when the raw word already held the value. */
static int res_set_knob(gm2_firmware *self, unsigned knob, int value) {
  gm2_emu *emu = self->emu;
  const unsigned slot = self->descriptor[knob];
  const uint32_t want = (uint32_t)value & 0xFFFFFFu;
  const int changed = gm2_read(emu, GM2_X, RES_RAW + slot) != want;
  self->raw[slot] = (int32_t)value;
  gm2_write(emu, GM2_X, RES_RAW + slot, want);
  if (changed != 0) {
    /* The design's table walks need M0 at the no-wrap 0xFFFFFF the background runs
     * with; the reset register file the activation enters from is not one. */
    gm2_set_reg(emu, DSP56300_REG_M0, 0xFFFFFFu);
    if (res_apply(self, self->callback[knob], RES_ALGORITHM, want) != 0) {
      fprintf(stderr, "%s: the callback for %s did not return\n", self->effect->name,
              self->effect->knob[knob].name);
      return -1;
    }
    if ((self->format[knob] & RES_FORMAT_SECOND) == RES_FORMAT_SECOND &&
        res_apply(self, self->callback[knob], RES_SECOND_ARG, want) != 0) {
      fprintf(stderr, "%s: the second call for %s did not return\n", self->effect->name,
              self->effect->knob[knob].name);
      return -1;
    }
    res_restore(self);
  }
  if (res_rebuild(self, knob) != 0) {
    return -1;
  }
  res_restore(self);
  self->audio_dirty = 1;
  return res_check_jobs(self);
}

static void res_wire(gm2_firmware *self) {
  gm2_emu *emu = self->emu;
  gm2_firmware_audio_reset(emu);
  gm2_write(emu, GM2_Y, RES_INPUT_POINTER, RES_INPUT);
  gm2_write(emu, GM2_Y, RES_DRY_POINTER, RES_INPUT);
  gm2_write(emu, GM2_Y, RES_OUTPUT_POINTER, RES_OUTPUT);
  gm2_write(emu, GM2_Y, RES_LINK_POINTER, RES_LINK_RECORD);
  const uint32_t link[2] = {GM2_STUB_ADDR + 2u, RES_OUTPUT};
  gm2_write_run(emu, GM2_Y, RES_LINK_RECORD, link, 2);
  self->audio_dirty = 0;
}

/* The coefficient job onto the live words until no word differs: the job snaps once
 * the gap is within its threshold. */
static int res_settle(gm2_firmware *self) {
  for (unsigned run = 0; run < RES_SETTLE_LIMIT; run++) {
    unsigned moved = 0;
    for (unsigned i = 0; i < GM2_RES_CORE_COEFS; i++) {
      if (gm2_read(self->emu, GM2_Y, RES_LIVE + i) != gm2_read(self->emu, GM2_X, RES_STAGED + i)) {
        moved++;
      }
    }
    if (moved == 0u) {
      return 0;
    }
    if (res_call(self, GM2_JOB_GROUP, 0u, RES_JOB, RES_BUDGET) != 0) {
      return -1;
    }
  }
  fprintf(stderr, "%s: the coefficient job never settled\n", self->effect->name);
  return -1;
}

/* The records the firmware holds for the block, in list order: the queue first,
 * then the active list. */
static unsigned res_jobs(const gm2_firmware *self, uint32_t *record) {
  const unsigned count = gm2_firmware_jobs(self->emu, record, RES_JOBS_MAX);
  if (count == 0u) {
    fprintf(stderr, "%s: cannot read the block's job records\n", self->effect->name);
  }
  return count;
}

/* The block holds the coefficient job and the level record and nothing else: the
 * adapter runs this list, so a third record, or a missing one, fails here. */
static int res_check_jobs(const gm2_firmware *self) {
  static const uint32_t k_handler[2] = {GM2_JOB_GROUP, GM2_JOB_SMOOTH};
  static const uint32_t k_params[2] = {RES_JOB, RES_JOB_LEVEL};
  uint32_t record[RES_JOBS_MAX];
  unsigned found = 0;
  const unsigned count = res_jobs(self, record);
  if (count == 0u) {
    return -1;
  }
  for (unsigned i = 0; i < count; i++) {
    const uint32_t handler = gm2_read(self->emu, GM2_X, record[i] + GM2_JOB_HANDLER);
    const uint32_t params = gm2_read(self->emu, GM2_X, record[i] + GM2_JOB_PARAMS);
    unsigned at = 0;
    while (at < 2u && (k_handler[at] != handler || k_params[at] != params)) {
      at++;
    }
    if (at == 2u) {
      fprintf(stderr, "%s: job X:0x%06X runs P:0x%06X on X:0x%06X, not a Filter block record\n",
              self->effect->name, record[i], handler, params);
      return -1;
    }
    found |= 1u << at;
  }
  if (found != 3u) {
    fprintf(stderr, "%s: the block holds records %u of its coefficient and level jobs\n",
            self->effect->name, found);
    return -1;
  }
  return 0;
}

static int res_run(gm2_firmware *self, int32_t left, int32_t right, int32_t *out) {
  gm2_emu *emu = self->emu;
  gm2_write(emu, GM2_X, RES_INPUT, gm2_word24(left));
  gm2_write(emu, GM2_X, RES_INPUT + 1u, gm2_word24(right));
  gm2_write(emu, GM2_X, RES_CHUNK_WORD, 4u - (uint32_t)(self->frame & 3u));
  if (self->subtype == 4u) {
    gm2_set_reg(emu, DSP56300_REG_R6, RES_GATE_TASK);
    if (gm2_run_until_stop(emu, RES_GATE_ENTRY, RES_BUDGET, NULL) != GM2_CALL_OK) {
      fprintf(stderr, "%s: the gate's frame %u never reached the stop\n", self->effect->name,
              self->frame);
      return -1;
    }
  }
  gm2_set_reg(emu, DSP56300_REG_R6, RES_TASK + 1u);
  if (gm2_run_until_stop(emu, RES_AUDIO_ENTRY, RES_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "%s: frame %u never reached the stop\n", self->effect->name, self->frame);
    return -1;
  }
  self->frame++;
  if (out != NULL) {
    out[0] = gm2_sign24(gm2_read(emu, GM2_X, RES_OUTPUT));
    out[1] = gm2_sign24(gm2_read(emu, GM2_X, RES_OUTPUT + 1u));
  }
  if ((self->frame & 3u) != 0u) {
    return 0;
  }
  if (gm2_firmware_run_jobs(self->emu, self->effect->name, RES_JOBS_MAX) != 0) {
    return -1;
  }
  if (++self->chunks < self->tick_chunks) {
    return 0;
  }
  self->chunks = 0;
  /* Subtype 3 answers no message 13. */
  if (self->subtype != 4u) {
    return 0;
  }
  /* P:0x090494 reads and resets the gate's peak hold, then P:0x091CF9 designs. */
  if (res_call(self, RES_GATE_READ, 0u, RES_GATE_UI, RES_BUDGET) != 0) {
    return -1;
  }
  int32_t level = gm2_sign24(gm2_get_reg(emu, DSP56300_REG_A1));
  gm2_write(emu, GM2_X, RES_LEVEL_WORD, gm2_word24(level));
  return res_call(self, RES_TICK_ENTRY, RES_ALGORITHM, RES_UI, RES_BUDGET);
}

static int res_start(gm2_firmware *self) {
  gm2_emu *emu = self->emu;
  if (self->subtype == 4u && res_gate(self) != 0) {
    return -1;
  }
  if (res_select(self) != 0) {
    return -1;
  }
  gm2_write(emu, GM2_X, RES_UI, RES_ALGORITHM);
  for (unsigned word = 0; word < RES_RAW_WORDS; word++) {
    gm2_write(emu, GM2_X, RES_RAW + word, (uint32_t)self->raw[word] & 0xFFFFFFu);
  }
  /* The first pass installs the level struct, the second runs with it in place.
   * Each enters from the reset register file the device's activation runs with; the
   * level code's staging words depend on it. */
  for (unsigned pass = 0; pass < 2u; pass++) {
    gm2_write(emu, GM2_X, RES_UI + 1u, pass == 0u ? 1u : 0u);
    gm2_reset_regs(emu);
    if (res_call(self, RES_ACTIVATION, RES_ALGORITHM, RES_UI, RES_BUDGET) != 0) {
      return -1;
    }
  }
  uint32_t levels[3];
  for (unsigned i = 0; i < 3u; i++) {
    levels[i] = gm2_read(emu, GM2_X, RES_LEVEL_STAGED + i);
  }
  gm2_write_run(emu, GM2_Y, RES_LEVEL, levels, 3);
  for (unsigned i = 0; i < RES_STATE_WORDS; i++) {
    gm2_write(emu, GM2_X, RES_STATE + i, 0);
    gm2_write(emu, GM2_Y, RES_STATE + i, 0);
  }
  if (res_descriptor(self) != 0) {
    return -1;
  }
  if (res_check_jobs(self) != 0) {
    return -1;
  }
  self->frame = 0;
  self->chunks = 0;
  res_wire(self);
  return res_settle(self);
}

GM2_FIRMWARE_CLOSE(res_close)

static gm2_firmware *res_open(const gm2_effect *effect, unsigned subtype,
                              const unsigned *descriptor, unsigned count) {
  if (count != effect->knobs || count > RES_KNOB_MAX) {
    fprintf(stderr, "%s: %u descriptor indices for %u knobs, at most %u\n", effect->name, count,
            effect->knobs, RES_KNOB_MAX);
    return NULL;
  }
  gm2_firmware *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->effect = effect;
  self->subtype = subtype;
  self->knobs = effect->knobs;
  for (unsigned knob = 0; knob < count; knob++) {
    self->descriptor[knob] = descriptor[knob];
  }
  self->emu = gm2_firmware_boot(effect->name);
  if (self->emu == NULL) {
    free(self);
    return NULL;
  }
  self->raw[RES_SUBTYPE_INDEX] = (int32_t)subtype;
  self->raw[RES_MIX_INDEX] = 100;
  for (unsigned knob = 0; knob < count; knob++) {
    self->raw[descriptor[knob]] = effect->knob[knob].def;
  }
  self->tick_chunks = gm2_tick_chunks(GM2_EFFECT_RATE);
  if (res_start(self) != 0) {
    res_close(self);
    return NULL;
  }
  return self;
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int res_levels(gm2_firmware *self, int32_t level[3]) {
  for (unsigned i = 0; i < 3u; i++) {
    uint32_t live = gm2_read(self->emu, GM2_Y, RES_LEVEL + i);
    uint32_t want = gm2_read(self->emu, GM2_X, RES_LEVEL_STAGED + i);
    if (live != want) {
      fprintf(stderr, "%s: level word Y:0x%06X is %06X, X:0x%06X stages %06X\n", self->effect->name,
              RES_LEVEL + i, live, RES_LEVEL_STAGED + i, want);
      return -1;
    }
    level[i] = gm2_sign24(live);
  }
  return 0;
}

static int res_frame(gm2_firmware *self, int32_t left, int32_t right, int32_t *out_left,
                     int32_t *out_right) {
  if (self->audio_dirty != 0) {
    res_wire(self);
  }
  int32_t out[2] = {0, 0};
  if (res_run(self, left, right, out) != 0) {
    return -1;
  }
  *out_left = out[0];
  *out_right = out[1];
  return 0;
}

/* The pair the level code staged for the live words at Y:0x3EE. */
static void res_mix_words(const gm2_firmware *self, int32_t *words) {
  words[0] = gm2_sign24(gm2_read(self->emu, GM2_X, RES_LEVEL_STAGED + 1u));
  words[1] = gm2_sign24(gm2_read(self->emu, GM2_X, RES_LEVEL_STAGED + 2u));
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int resfilter_words(gm2_firmware *self, int32_t *words) {
  for (unsigned i = 0; i < GM2_RES_CORE_COEFS; i++) {
    words[i] = gm2_sign24(gm2_read(self->emu, GM2_X, RES_STAGED + i));
  }
  res_mix_words(self, words + GM2_RES_CORE_COEFS);
  return 0;
}

/* The Touch Res Filter's coefficient set is what its tick designs at rest: the
 * follower at zero reading level zero, which is the state the effect starts in. The
 * runtime follower and level word go back, so a readout leaves the run alone. */
static int touchres_rest_design(gm2_firmware *self) {
  gm2_emu *emu = self->emu;
  uint32_t follower[2];
  for (unsigned i = 0; i < 2u; i++) {
    follower[i] = gm2_read(emu, GM2_X, RES_UI + 36u + i);
    gm2_write(emu, GM2_X, RES_UI + 36u + i, 0);
  }
  uint32_t level = gm2_read(emu, GM2_X, RES_LEVEL_WORD);
  gm2_write(emu, GM2_X, RES_LEVEL_WORD, 0);
  int status = res_call(self, RES_TICK_ENTRY, RES_ALGORITHM, RES_UI, RES_BUDGET);
  for (unsigned i = 0; i < 2u; i++) {
    gm2_write(emu, GM2_X, RES_UI + 36u + i, follower[i]);
  }
  gm2_write(emu, GM2_X, RES_LEVEL_WORD, level);
  return status;
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int touchres_words(gm2_firmware *self, int32_t *words) {
  if (touchres_rest_design(self) != 0) {
    return -1;
  }
  resfilter_words(self, words);
  for (unsigned pair = 0; pair < RES_TICK_PAIRS; pair++) {
    for (unsigned half = 0; half < 2u; half++) {
      words[GM2_RES_CORE_COEFS + 2u * pair + half] =
          gm2_sign24(gm2_read(self->emu, GM2_X, k_touchres_pairs[pair] + half));
    }
  }
  res_mix_words(self, words + GM2_RES_CORE_COEFS + 12u);
  return 0;
}

static gm2_firmware *resfilter_open(void) {
  return res_open(&gm2_effect_resfilter, 3u, k_resfilter_descriptor,
                  sizeof k_resfilter_descriptor / sizeof k_resfilter_descriptor[0]);
}

static gm2_firmware *touchres_open(void) {
  return res_open(&gm2_effect_touchres, 4u, k_touchres_descriptor,
                  sizeof k_touchres_descriptor / sizeof k_touchres_descriptor[0]);
}

const gm2_firmware_ops gm2_firmware_resfilter = {
    .effect = &gm2_effect_resfilter,
    .open = resfilter_open,
    .levels = res_levels,
    .set_knob = res_set_knob,
    .words = resfilter_words,
    .frame = res_frame,
    .close = res_close,
};

const gm2_firmware_ops gm2_firmware_touchres = {
    .effect = &gm2_effect_touchres,
    .open = touchres_open,
    .levels = res_levels,
    .set_knob = res_set_knob,
    .words = touchres_words,
    .frame = res_frame,
    .close = res_close,
};
