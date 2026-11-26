/* The Filter block's three wahs as the firmware runs them: the activation
 * P:0x09233B with its parameter updaters, the parameter edit P:0x083268, the audio
 * continuation P:0x000F24 with its task record Y:0x3F1, the frequency, step and
 * level jobs, and the tick P:0x092541.
 */
#include "firmware_common.h"
#include "gm2_autowah.h"
#include "gm2_effect.h"
#include "gm2_emu.h"
#include "gm2_record.h"
#include "gm2_tick.h"
#include "gm2_touchwah.h"
#include "gm2_wahwah.h"

#include <dsp56300.h>

#include <stdio.h>
#include <stdlib.h>

#define WAH_UI 0x0989B4u
#define WAH_RAW 0x099B46u
#define WAH_RAW_WORDS 16u
#define WAH_ALGORITHM 1u
#define WAH_SUBTYPE_INDEX 0u
#define WAH_MIX_INDEX 13u
#define WAH_OUT_LEVEL_INDEX 14u
#define WAH_TAP_TIME 0x099A6Bu
#define WAH_ACTIVATION 0x09233Bu
#define WAH_TEMPO_CALLBACK 0x09274Bu
/* X:0x09860C, the value an edit hands to a callback that writes a parameter back:
 * the Tempo callback stages it into the Tempo slot before it reads the slot. */
#define WAH_EDIT_VALUE 0x09860Cu
/* The descriptor's format word at entry +12: with bits 23 and 13 set, the edit
 * P:0x083268 calls the callback a second time, A = 0x50 and the value at
 * X:0x09860C. The Auto Wah's Tempo entry is the only knob of the three that has
 * both bits. */
#define WAH_DESCRIPTOR_STRIDE 15u
#define WAH_DESCRIPTOR_FORMAT 12u
#define WAH_FORMAT_SECOND 0x802000u
#define WAH_SECOND_ARG 0x50u
/* The raw indices the Auto Wah's tempo path reads beside the turned knob. */
#define WAH_SPEED 4u
#define WAH_TEMPO 6u
/* The level code's publish record behind X:(UI+73): {src, dst, count}. */
#define WAH_LEVEL_PUBLISH (WAH_UI + 73u)
/* P:0x08F763 and the level struct at UI+60 it takes in R0. The Mix callback
 * P:0x09277A reaches it only when the block's gate word X:(UI+3) is clear, which
 * neither activation pass writes. */
#define WAH_LEVEL_CODE 0x08F763u
#define WAH_LEVEL_STRUCT (WAH_UI + 60u)
#define WAH_LEVEL 0x0419u
#define WAH_STAGED 0x09899Eu /* the staged frequency, then the staged LFO step */
#define WAH_AUDIO_ENTRY 0x000F24u
#define WAH_TASK 0x03F1u
#define WAH_INPUT 0x0600u
#define WAH_OUTPUT 0x0620u
#define WAH_LINK_RECORD 0x070Bu
#define WAH_INPUT_POINTER 0x03F2u
#define WAH_DRY_POINTER 0x03FFu
#define WAH_OUTPUT_POINTER 0x0400u
#define WAH_LINK_WORD 0x0401u
#define WAH_CHUNK_WORD 0x0001u
#define WAH_TICK 0x092541u
/* The Modulation block's waveform generator P:0x093820, which fills the six
 * 256-word sine tables the LFO's sine shape reads at X:0x1000. */
#define WAH_SINE_GENERATOR 0x093820u
#define WAH_SINE_SCRATCH 0x0F00u
#define WAH_SINE_SCRATCH_WORDS 8u
#define WAH_JOB_FREQUENCY 0x0989A0u
#define WAH_JOB_STEP 0x0989A6u
#define WAH_JOB_LEVEL 0x0989FDu
#define WAH_JOBS_MAX 8u
#define WAH_SHIFT 0x040Au
#define WAH_SWEEP 0x040Eu
#define WAH_SHAPE 0x0414u
#define WAH_DECAY 0x0415u
#define WAH_PEAK 0x0418u
#define WAH_ENVELOPE 0x02C0u
#define WAH_PHASE 0x02BCu
#define WAH_SMOOTH 0x02BDu
#define WAH_LFO_OUT 0x02BEu
#define WAH_TRIANGLE 0x02BFu
#define WAH_FILTER_STATE 0x000Eu /* L:0x00E..0x011, two 48-bit words a channel */
#define WAH_FILTER_STATE_WORDS 4u
#define WAH_FOLLOWER (WAH_UI + 50u) /* UI+50/51, exponent word first */
#define WAH_LFO_START 0x400000u     /* the LFO's word at phase 0, both shapes */
#define WAH_EMPTY_PEAK 0x800000u
#define WAH_SETTLE_LIMIT 400000
/* The Touch Wah's and Auto Wah's follower words, each pair exponent word first. */
#define WAH_TOUCHWAH_FOLLOWER_WORDS 8u
#define WAH_KNOB_MAX 6u

struct gm2_firmware {
  gm2_emu *emu;
  const gm2_effect *effect;
  unsigned subtype;
  unsigned knobs;
  unsigned descriptor[WAH_KNOB_MAX];
  uint32_t callback[WAH_KNOB_MAX];
  uint32_t format[WAH_KNOB_MAX];
  int32_t raw[WAH_RAW_WORDS];
  unsigned follower[WAH_TOUCHWAH_FOLLOWER_WORDS];
  unsigned follower_words;
  unsigned tick_chunks;
  unsigned chunks;
  unsigned frame; /* frames since the rest state, for the chunk word */
  int audio_dirty;
};

static const unsigned k_wahwah_descriptor[] = {1u, 9u, 13u, WAH_OUT_LEVEL_INDEX};
static const unsigned k_touchwah_descriptor[] = {2u, 3u, 9u, 13u, WAH_OUT_LEVEL_INDEX};
static const unsigned k_autowah_descriptor[] = {4u, 6u, 7u, 9u, 13u, WAH_OUT_LEVEL_INDEX};
static const unsigned k_touchwah_follower[] = {46u, 47u, 48u, 49u, 52u, 53u, 54u, 55u};

/* A: the block index the device's edit path passes, R0: what the updaters read,
 * R7: their stack. M0 stays at 0xFFFFFF, the value a background call runs with. */
static int wah_call(gm2_firmware *self, uint32_t target, uint32_t a, uint32_t r0, int32_t budget) {
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

/* The sub-record the subtype's dispatcher selects and the descriptor table it
 * points at. */
static int wah_select(gm2_firmware *self) {
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

static int wah_descriptor(gm2_firmware *self) {
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
                 table + WAH_DESCRIPTOR_STRIDE * self->descriptor[knob] + WAH_DESCRIPTOR_FORMAT);
  }
  return 0;
}

/* One callback with the registers the device's edit path P:0x083268 hands it: the
 * algorithm in A, 0x50 for the second call it makes, and the block's raw array in
 * R0. The wrappers every descriptor entry names take R0 from themselves, and the
 * Tempo callback P:0x09274B hands P:0x0924C1 M0 as its tempo flag, so M0 stays at
 * the no-wrap 0xFFFFFF the background runs with. */
static int wah_apply(gm2_firmware *self, uint32_t callback, uint32_t a, uint32_t r0,
                     uint32_t value) {
  gm2_emu *emu = self->emu;
  gm2_write(emu, GM2_X, WAH_EDIT_VALUE, value & 0xFFFFFFu);
  gm2_set_reg(emu, DSP56300_REG_M0, 0xFFFFFFu);
  if (wah_call(self, callback, a, r0, GM2_FIRMWARE_CALL_BUDGET) != 0) {
    return -1;
  }
  return 0;
}

/* The knob vector is the host's: a word a callback rewrote goes back without its
 * own callback. That write-back is the nearest Speed step P:0x0924C1's tempo path
 * puts in the Speed slot. */
static void wah_restore(gm2_firmware *self) {
  for (unsigned knob = 0; knob < self->knobs; knob++) {
    const unsigned slot = self->descriptor[knob];
    const uint32_t want = (uint32_t)self->raw[slot] & 0xFFFFFFu;
    if (gm2_read(self->emu, GM2_X, WAH_RAW + slot) != want) {
      gm2_write(self->emu, GM2_X, WAH_RAW + slot, want);
    }
  }
}

/* The rebuilds a turn needs beyond its own callback: a Mix or Out Level turn's
 * level code, and the Tempo rule's step. The level code is what the callbacks
 * P:0x09277A and P:0x0926E3 run themselves once the block's gate word X:(UI+3) is
 * clear, which neither activation pass writes, so the adapter hands it the struct
 * and the value the callback would. The Tempo rule: the Speed callback P:0x09273A
 * clears A before P:0x0924C1, so a Speed turn restages the Speed rate's step even
 * under a Tempo value, and the Tempo callback's tempo path has to put the vector's
 * step back. The Tap Time refresh P:0x08D2B0 runs that same path. */
static int wah_rebuild(gm2_firmware *self, unsigned knob) {
  if (self->descriptor[knob] == WAH_MIX_INDEX || self->descriptor[knob] == WAH_OUT_LEVEL_INDEX) {
    return wah_call(self, WAH_LEVEL_CODE, 0u, WAH_LEVEL_STRUCT, GM2_FIRMWARE_CALL_BUDGET);
  }
  if (self->subtype != 2u || self->descriptor[knob] != WAH_SPEED) {
    return 0;
  }
  const int32_t tempo = self->raw[WAH_TEMPO];
  if (tempo == 0) {
    return 0;
  }
  if (wah_apply(self, WAH_TEMPO_CALLBACK, WAH_SECOND_ARG, WAH_RAW, (uint32_t)tempo) != 0) {
    fprintf(stderr, "%s: the Tempo callback did not return\n", self->effect->name);
    return -1;
  }
  return 0;
}

static int wah_check_jobs(const gm2_firmware *self);

/* The device's parameter edit P:0x083268 for one turn: the raw word, the entry's
 * callback, the second call when the format word asks for it, then the rebuild.
 * The edit returns before the callback when the raw word already held the value. */
static int wah_set_knob(gm2_firmware *self, unsigned knob, int value) {
  gm2_emu *emu = self->emu;
  const unsigned slot = self->descriptor[knob];
  const uint32_t want = (uint32_t)value & 0xFFFFFFu;
  const int changed = gm2_read(emu, GM2_X, WAH_RAW + slot) != want;
  self->raw[slot] = (int32_t)value;
  gm2_write(emu, GM2_X, WAH_RAW + slot, want);
  if (changed != 0) {
    if (wah_apply(self, self->callback[knob], WAH_ALGORITHM, WAH_RAW, want) != 0) {
      fprintf(stderr, "%s: the callback for %s did not return\n", self->effect->name,
              self->effect->knob[knob].name);
      return -1;
    }
    if ((self->format[knob] & WAH_FORMAT_SECOND) == WAH_FORMAT_SECOND &&
        wah_apply(self, self->callback[knob], WAH_SECOND_ARG, WAH_RAW, want) != 0) {
      fprintf(stderr, "%s: the second call for %s did not return\n", self->effect->name,
              self->effect->knob[knob].name);
      return -1;
    }
    wah_restore(self);
  }
  if (wah_rebuild(self, knob) != 0) {
    return -1;
  }
  wah_restore(self);
  self->audio_dirty = 1;
  return wah_check_jobs(self);
}

/* The continuation's own registers and the words that point it at its buffers. */
static void wah_wire(gm2_firmware *self) {
  gm2_emu *emu = self->emu;
  gm2_firmware_audio_reset(emu);
  gm2_write(emu, GM2_Y, WAH_INPUT_POINTER, WAH_INPUT);
  gm2_write(emu, GM2_Y, WAH_DRY_POINTER, WAH_INPUT);
  gm2_write(emu, GM2_Y, WAH_OUTPUT_POINTER, WAH_OUTPUT);
  gm2_write(emu, GM2_Y, WAH_LINK_WORD, WAH_LINK_RECORD);
  const uint32_t link[2] = {GM2_STUB_ADDR + 2u, WAH_OUTPUT};
  gm2_write_run(emu, GM2_Y, WAH_LINK_RECORD, link, 2);
  self->audio_dirty = 0;
}

static void wah_silent_filter(gm2_firmware *self) {
  for (unsigned i = 0; i < WAH_FILTER_STATE_WORDS; i++) {
    gm2_write(self->emu, GM2_X, WAH_FILTER_STATE + i, 0);
    gm2_write(self->emu, GM2_Y, WAH_FILTER_STATE + i, 0);
  }
}

/* One run of a smoothing job, repeated until no live word differs from its staged
 * one: the job snaps onto it once the gap is within the record's threshold. */
static int wah_settle(gm2_firmware *self, uint32_t job) {
  uint32_t source = gm2_read(self->emu, GM2_X, job);
  uint32_t target = gm2_read(self->emu, GM2_X, job + 1u);
  uint32_t words = gm2_read(self->emu, GM2_X, job + 2u);
  for (unsigned run = 0; run < WAH_SETTLE_LIMIT; run++) {
    unsigned moved = 0;
    for (uint32_t i = 0; i < words; i++) {
      if (gm2_read(self->emu, GM2_Y, target + i) != gm2_read(self->emu, GM2_X, source + i)) {
        moved++;
      }
    }
    if (moved == 0u) {
      return 0;
    }
    if (wah_call(self, GM2_JOB_SMOOTH, WAH_ALGORITHM, job, GM2_FIRMWARE_CALL_BUDGET) != 0) {
      return -1;
    }
  }
  fprintf(stderr, "%s: job X:0x%06X never settled\n", self->effect->name, job);
  return -1;
}

/* The records the firmware holds for the block, in list order: the queue first,
 * then the active list. */
static unsigned wah_jobs(const gm2_firmware *self, uint32_t *record) {
  const unsigned count = gm2_firmware_jobs(self->emu, record, WAH_JOBS_MAX);
  if (count == 0u) {
    fprintf(stderr, "%s: cannot read the block's job records\n", self->effect->name);
  }
  return count;
}

/* The block holds the frequency, step and level records and nothing else: the
 * adapter runs this list, so a fourth record -- a P:0x09605F ramp arm, say -- or a
 * missing one fails here. */
static int wah_check_jobs(const gm2_firmware *self) {
  static const uint32_t k_params[3] = {WAH_JOB_FREQUENCY, WAH_JOB_STEP, WAH_JOB_LEVEL};
  uint32_t record[WAH_JOBS_MAX];
  unsigned found = 0;
  const unsigned count = wah_jobs(self, record);
  if (count == 0u) {
    return -1;
  }
  for (unsigned i = 0; i < count; i++) {
    const uint32_t handler = gm2_read(self->emu, GM2_X, record[i] + GM2_JOB_HANDLER);
    const uint32_t params = gm2_read(self->emu, GM2_X, record[i] + GM2_JOB_PARAMS);
    unsigned at = 0;
    while (at < 3u && k_params[at] != params) {
      at++;
    }
    if (handler != GM2_JOB_SMOOTH || at == 3u) {
      fprintf(stderr, "%s: job X:0x%06X runs P:0x%06X on X:0x%06X, not a Filter block record\n",
              self->effect->name, record[i], handler, params);
      return -1;
    }
    found |= 1u << at;
  }
  if (found != 7u) {
    fprintf(stderr, "%s: the block holds records %u of its frequency, step and level jobs\n",
            self->effect->name, found);
    return -1;
  }
  return 0;
}

/* One frame of the continuation, `out` when it is wanted, then the jobs the chunk
 * owes and the tick the period owes, all before the next frame. */
static int wah_run(gm2_firmware *self, int32_t left, int32_t right, int32_t *out) {
  gm2_emu *emu = self->emu;
  gm2_write(emu, GM2_X, WAH_INPUT, gm2_word24(left));
  gm2_write(emu, GM2_X, WAH_INPUT + 1u, gm2_word24(right));
  gm2_write(emu, GM2_X, WAH_CHUNK_WORD, 4u - (uint32_t)(self->frame & 3u));
  gm2_set_reg(emu, DSP56300_REG_R6, WAH_TASK + 1u);
  if (gm2_run_until_stop(emu, WAH_AUDIO_ENTRY, GM2_FIRMWARE_CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "%s: frame %u never reached the stop\n", self->effect->name, self->frame);
    return -1;
  }
  self->frame++;
  if (out != NULL) {
    out[0] = gm2_sign24(gm2_read(emu, GM2_X, WAH_OUTPUT));
    out[1] = gm2_sign24(gm2_read(emu, GM2_X, WAH_OUTPUT + 1u));
  }
  if ((self->frame & 3u) != 0u) {
    return 0;
  }
  if (gm2_firmware_run_jobs(self->emu, self->effect->name, WAH_JOBS_MAX) != 0) {
    return -1;
  }
  if (++self->chunks < self->tick_chunks) {
    return 0;
  }
  self->chunks = 0;
  /* Subtype 0 answers no message 13. */
  return self->subtype == 0u
             ? 0
             : wah_call(self, WAH_TICK, WAH_ALGORITHM, WAH_UI, GM2_FIRMWARE_CALL_BUDGET);
}

/* The rest state the native state_init starts in: silent filter, envelope and
 * LFO, an empty peak hold, the sweep word the effect starts on, and the live
 * frequency and LFO step settled on the staged ones. The staged frequency is the
 * routine's own: one silent frame's chunk code computes it. */
static int wah_rest(gm2_firmware *self) {
  gm2_emu *emu = self->emu;
  const uint32_t lfo = self->subtype == 2u ? WAH_LFO_START : 0u;
  wah_silent_filter(self);
  gm2_write(emu, GM2_Y, WAH_PEAK, WAH_EMPTY_PEAK);
  gm2_write(emu, GM2_X, WAH_FOLLOWER, 0);
  gm2_write(emu, GM2_X, WAH_FOLLOWER + 1u, 0);
  if (self->subtype == 1u) {
    /* The sweep word a tick in silence writes for a follower at zero. */
    if (wah_call(self, WAH_TICK, WAH_ALGORITHM, WAH_UI, GM2_FIRMWARE_CALL_BUDGET) != 0) {
      return -1;
    }
  } else if (self->subtype == 2u) {
    gm2_write(emu, GM2_Y, WAH_SWEEP, WAH_LFO_START);
  }
  gm2_write(emu, GM2_X, WAH_SMOOTH, lfo);
  gm2_write(emu, GM2_X, WAH_LFO_OUT, lfo);
  if (wah_run(self, 0, 0, NULL) != 0) {
    return -1;
  }
  gm2_write(emu, GM2_X, WAH_ENVELOPE, 0);
  gm2_write(emu, GM2_X, WAH_PHASE, 0);
  gm2_write(emu, GM2_X, WAH_SMOOTH, lfo);
  gm2_write(emu, GM2_X, WAH_LFO_OUT, lfo);
  gm2_write(emu, GM2_X, WAH_TRIANGLE, 0);
  gm2_write(emu, GM2_Y, WAH_PEAK, WAH_EMPTY_PEAK);
  wah_silent_filter(self);
  /* The first frame the caller processes is a chunk's first, as the native state
   * starts it. */
  self->frame = 0;
  return wah_settle(self, WAH_JOB_FREQUENCY) == 0 && wah_settle(self, WAH_JOB_STEP) == 0 ? 0 : -1;
}

static int wah_start(gm2_firmware *self) {
  gm2_emu *emu = self->emu;
  gm2_write(emu, GM2_X, WAH_UI, WAH_ALGORITHM);
  for (unsigned word = 0; word < WAH_RAW_WORDS; word++) {
    gm2_write(emu, GM2_X, WAH_RAW + word, (uint32_t)self->raw[word] & 0xFFFFFFu);
  }
  gm2_write(emu, GM2_X, WAH_TAP_TIME, GM2_EFFECT_BEAT_MS);
  for (unsigned word = 0; word < WAH_SINE_SCRATCH_WORDS; word++) {
    gm2_write(emu, GM2_X, WAH_SINE_SCRATCH + word, 0);
  }
  if (wah_call(self, WAH_SINE_GENERATOR, WAH_ALGORITHM, WAH_SINE_SCRATCH,
               GM2_FIRMWARE_CALL_BUDGET) != 0) {
    return -1;
  }
  if (wah_select(self) != 0 || wah_descriptor(self) != 0) {
    return -1;
  }
  /* The first pass installs the level struct, the second runs with it in place. */
  for (unsigned pass = 0; pass < 2u; pass++) {
    gm2_write(emu, GM2_X, WAH_UI + 1u, pass == 0u ? 1u : 0u);
    if (wah_call(self, WAH_ACTIVATION, WAH_ALGORITHM, WAH_UI, GM2_FIRMWARE_CALL_BUDGET) != 0) {
      return -1;
    }
  }
  uint32_t staged = gm2_read(emu, GM2_X, WAH_LEVEL_PUBLISH);
  if (gm2_read(emu, GM2_X, WAH_LEVEL_PUBLISH + 1u) != WAH_LEVEL ||
      gm2_read(emu, GM2_X, WAH_LEVEL_PUBLISH + 2u) != 3u) {
    fprintf(stderr, "%s: the level code publishes %06X onto Y:%06X over %u words\n",
            self->effect->name, staged, gm2_read(emu, GM2_X, WAH_LEVEL_PUBLISH + 1u),
            gm2_read(emu, GM2_X, WAH_LEVEL_PUBLISH + 2u));
    return -1;
  }
  uint32_t level[3];
  for (unsigned i = 0; i < 3u; i++) {
    level[i] = gm2_read(emu, GM2_X, staged + i);
  }
  gm2_write_run(emu, GM2_Y, WAH_LEVEL, level, 3);
  /* The activation runs the Speed branch of P:0x0924C1; only the callbacks stage a
   * Tempo value's step, so the rest state runs them all once. */
  for (unsigned knob = 0; knob < self->knobs; knob++) {
    const uint32_t a = self->callback[knob] == WAH_TEMPO_CALLBACK ? WAH_SECOND_ARG : WAH_ALGORITHM;
    if (wah_apply(self, self->callback[knob], a, WAH_UI,
                  (uint32_t)self->raw[self->descriptor[knob]]) != 0) {
      return -1;
    }
  }
  wah_restore(self);
  if (wah_check_jobs(self) != 0) {
    return -1;
  }
  self->frame = 0;
  self->chunks = 0;
  wah_wire(self);
  return wah_rest(self);
}

GM2_FIRMWARE_CLOSE(wah_close)

static gm2_firmware *wah_open(const gm2_effect *effect, unsigned subtype,
                              const unsigned *descriptor, unsigned count, const unsigned *follower,
                              unsigned follower_words) {
  if (count != effect->knobs || count > WAH_KNOB_MAX) {
    fprintf(stderr, "%s: %u descriptor indices for %u knobs, at most %u\n", effect->name, count,
            effect->knobs, WAH_KNOB_MAX);
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
  for (unsigned i = 0; i < follower_words; i++) {
    self->follower[i] = follower[i];
  }
  self->follower_words = follower_words;
  self->emu = gm2_firmware_boot(effect->name);
  if (self->emu == NULL) {
    free(self);
    return NULL;
  }
  self->raw[WAH_SUBTYPE_INDEX] = (int32_t)subtype;
  self->raw[WAH_MIX_INDEX] = 100;
  for (unsigned knob = 0; knob < count; knob++) {
    self->raw[descriptor[knob]] = effect->knob[knob].def;
  }
  self->tick_chunks = gm2_tick_chunks(GM2_EFFECT_RATE);
  if (wah_start(self) != 0) {
    wah_close(self);
    return NULL;
  }
  return self;
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int wah_levels(gm2_firmware *self, int32_t level[3]) {
  uint32_t staged = gm2_read(self->emu, GM2_X, WAH_LEVEL_PUBLISH);
  for (unsigned i = 0; i < 3u; i++) {
    uint32_t live = gm2_read(self->emu, GM2_Y, WAH_LEVEL + i);
    uint32_t want = gm2_read(self->emu, GM2_X, staged + i);
    if (live != want) {
      fprintf(stderr, "%s: level word Y:0x%06X is %06X, X:0x%06X stages %06X\n", self->effect->name,
              WAH_LEVEL + i, live, staged + i, want);
      return -1;
    }
    level[i] = gm2_sign24(live);
  }
  return 0;
}

static int wah_frame(gm2_firmware *self, int32_t left, int32_t right, int32_t *out_left,
                     int32_t *out_right) {
  if (self->audio_dirty != 0) {
    wah_wire(self);
  }
  int32_t out[2] = {0, 0};
  if (wah_run(self, left, right, out) != 0) {
    return -1;
  }
  *out_left = out[0];
  *out_right = out[1];
  return 0;
}

/* The pair the level code staged for the live words at Y:0x419. */
static void wah_mix_words(const gm2_firmware *self, int32_t *words) {
  const uint32_t staged = gm2_read(self->emu, GM2_X, WAH_LEVEL_PUBLISH);
  words[0] = gm2_sign24(gm2_read(self->emu, GM2_X, staged + 1u));
  words[1] = gm2_sign24(gm2_read(self->emu, GM2_X, staged + 2u));
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int wahwah_words(gm2_firmware *self, int32_t *words) {
  words[0] = gm2_sign24(gm2_read(self->emu, GM2_Y, WAH_SHIFT));
  words[1] = gm2_sign24(gm2_read(self->emu, GM2_Y, WAH_SWEEP));
  wah_mix_words(self, words + 2);
  return 0;
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int touchwah_words(gm2_firmware *self, int32_t *words) {
  words[0] = gm2_sign24(gm2_read(self->emu, GM2_Y, WAH_SHIFT));
  words[1] = gm2_sign24(gm2_read(self->emu, GM2_Y, WAH_DECAY));
  for (unsigned i = 0; i < self->follower_words; i++) {
    words[2u + i] = gm2_sign24(gm2_read(self->emu, GM2_X, WAH_UI + self->follower[i]));
  }
  wah_mix_words(self, words + 2u + self->follower_words);
  return 0;
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int autowah_words(gm2_firmware *self, int32_t *words) {
  words[0] = gm2_sign24(gm2_read(self->emu, GM2_Y, WAH_SHIFT));
  words[1] = gm2_sign24(gm2_read(self->emu, GM2_Y, WAH_SHAPE));
  words[2] = gm2_sign24(gm2_read(self->emu, GM2_X, WAH_STAGED + 1u));
  wah_mix_words(self, words + 3);
  return 0;
}

static gm2_firmware *wahwah_open(void) {
  return wah_open(&gm2_effect_wahwah, 0u, k_wahwah_descriptor,
                  sizeof k_wahwah_descriptor / sizeof k_wahwah_descriptor[0], NULL, 0u);
}

static gm2_firmware *touchwah_open(void) {
  return wah_open(&gm2_effect_touchwah, 1u, k_touchwah_descriptor,
                  sizeof k_touchwah_descriptor / sizeof k_touchwah_descriptor[0],
                  k_touchwah_follower, WAH_TOUCHWAH_FOLLOWER_WORDS);
}

static gm2_firmware *autowah_open(void) {
  return wah_open(&gm2_effect_autowah, 2u, k_autowah_descriptor,
                  sizeof k_autowah_descriptor / sizeof k_autowah_descriptor[0], NULL, 0u);
}

const gm2_firmware_ops gm2_firmware_wahwah = {
    .effect = &gm2_effect_wahwah,
    .open = wahwah_open,
    .levels = wah_levels,
    .set_knob = wah_set_knob,
    .words = wahwah_words,
    .frame = wah_frame,
    .close = wah_close,
};

const gm2_firmware_ops gm2_firmware_touchwah = {
    .effect = &gm2_effect_touchwah,
    .open = touchwah_open,
    .levels = wah_levels,
    .set_knob = wah_set_knob,
    .words = touchwah_words,
    .frame = wah_frame,
    .close = wah_close,
};

const gm2_firmware_ops gm2_firmware_autowah = {
    .effect = &gm2_effect_autowah,
    .open = autowah_open,
    .levels = wah_levels,
    .set_knob = wah_set_knob,
    .words = autowah_words,
    .frame = wah_frame,
    .close = wah_close,
};
