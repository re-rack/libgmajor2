/* The Delay block (algorithm 4) as the firmware runs it: the subtype's descriptor
 * table P:0x085EB5/P:0x085FA5/P:0x086095/P:0x086185 through its own callbacks, the
 * block activation P:0x09423F, the nine job records X:0x098E39..X:0x098EC5 the
 * activation leaves queued, and the audio continuation P:0x001239 around the engine
 * routine P:0x0011F9 with its task record Y:0x04F9.
 *
 * The tap crossfade records name the handler P:0x000019, whose P:0x000018 word
 * points the manager's P:0x0960B4 at the check P:0x000301 that arms the crossfade
 * from the staged offset; running the record runs both.
 *
 * A standalone plugin has no preset spillover, so the engine the activation did
 * not build is muted by zeroing its input, feedback and output matrices: the one
 * active engine at X:0x030B / Y:0x0506 is the whole delay.
 *
 * The Dynamic Delay runs the Gate/EQ block for its level detector alone: the
 * gate's task record at Y:0x0360 reads the same input buffer, and P:0x090494
 * hands the peak hold Y:0x0377 to the ducking tick P:0x0944BE. The Reverse Delay
 * splices a grain every gm2_tick_period() chunks of its Y+26 length.
 */
#include "firmware_common.h"
#include "gm2_effect.h"
#include "gm2_emu.h"
#include "gm2_record.h"
#include "gm2_tick.h"

#include <dsp56300.h>

#include <stdio.h>
#include <stdlib.h>

#define DELAY_ALGORITHM 4u
#define DELAY_UI 0x098E7Du
#define DELAY_PARAMS 0x099B76u /* X: the live raw values, index 0..15 */
#define DELAY_PARAM_WORDS 16u
#define DELAY_DESCRIPTOR_WORDS 15u /* the record counts 0..14; BYPASS sits past it */
#define DELAY_VALUE_WORD 0x09860Cu /* X: the value the parameter path hands a callback */
#define DELAY_UI_POINTERS 0x098EB8u
#define DELAY_TAP_MASTER 0x099A66u
#define DELAY_BEAT_WORD 0x099A6Bu
#define DELAY_TAP_TIME 0x099A54u /* X: the clamped Tap Time the tempo paths read */
#define DELAY_ACTIVATION 0x09423Fu
#define DELAY_ENGINE_BUILDER 0x094336u
#define DELAY_ENTRY 0x001239u
#define DELAY_TASK_ENTRY 0x04FAu
#define DELAY_LINK_RECORD 0x070Bu
#define DELAY_INPUT 0x0600u
#define DELAY_OUTPUT 0x0620u
#define DELAY_ENGINE 0x030Bu
#define DELAY_COEF 0x0506u
#define DELAY_OTHER_COEF 0x0530u /* Y: the engine the activation did not build */
#define DELAY_ENGINE_WORDS 0x2Au /* Y: the twenty coefficients and their pointers */
#define DELAY_COEF_WORDS 20u
#define DELAY_LEVEL 0x055Au /* Y: the input, wet and dry words the level code reaches */
#define DELAY_LEVEL_WORDS 3u
#define DELAY_LEVEL_STAGED 0x098ECFu /* X: what P:0x08F763 wrote, the same three words */
#define DELAY_TAP_JOB0 0x098E23u
#define DELAY_TAP_JOB_STRIDE 11u
#define DELAY_TAP_STEP_WORD 0u
#define DELAY_TAP_STAGED_WORD 7u /* the record word P:0x094735 stages */
#define DELAY_TAP_ENTRY 0x000019u /* P:0x000019 jumps to the step P:0x000293 */
#define DELAY_TEMPO_ENTRY 0x094735u
#define DELAY_DELAY_CALLBACK 0x094B68u /* Dly, Dly2 and Offset */
#define DELAY_TEMPO_CALLBACK 0x094B96u /* Tempo and Tempo1 */
#define DELAY_TEMPO2_CALLBACK 0x094BB4u
#define DELAY_SPLICE 0x094424u
#define DELAY_GRAIN_WORD (DELAY_COEF + 26u) /* Y+26: the grain length in ms */
#define DELAY_DUCK_STATE (DELAY_ENGINE + 30u)
#define DELAY_DUCK_WORDS (DELAY_ENGINE + 26u) /* X+26..29: sense, damp, attack, release */
#define DELAY_DUCK_LEVEL 0x0B03u
#define DELAY_DUCK_TICK 0x0944BEu
#define DELAY_CHUNK_PHASE 0x0001u /* X: bit 2 marks a four-frame chunk's first frame */
#define DELAY_MUTE_WORDS 12u      /* the input, feedback and output matrices */
#define DELAY_SETTLE_LIMIT 100000u
#define DELAY_SETTLE_ROUNDS 20000u
#define DELAY_SETTLE_WORDS                                                                         \
  (DELAY_ENGINE_WORDS + DELAY_LEVEL_WORDS + 8u + 22u) /* the live words the jobs move */
#define DELAY_BUDGET 2000000

/* The Gate/EQ block, run for the Noise Gate's level detector only. */
#define GATE_ALGORITHM 6u
#define GATE_UI 0x0987A4u
#define GATE_ACTIVATION 0x09038Bu
#define GATE_LINK 0x090441u
#define GATE_READ 0x090494u
#define GATE_ENTRY 0x000C47u
#define GATE_TASK 0x0360u
#define GATE_LINK_RECORD 0x0700u
#define GATE_OUTPUT 0x0640u
#define GATE_STATE 0x02A0u
#define GATE_STATE_WORDS 5u
#define GATE_CONFIG 0x0371u
#define GATE_PEAK 0x0377u
#define GATE_LEVEL 0x0394u
#define GATE_BUDGET 400000

/* The descriptor index of every knob, in the knob table's order. */
static const uint8_t k_pingpong_index[] = {1, 3, 4, 5, 7, 8, 13};
static const uint8_t k_dyndelay_index[] = {1, 3, 5, 7, 8, 9, 10, 11, 12, 13};
static const uint8_t k_dualdelay_index[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 13};
static const uint8_t k_revdelay_index[] = {1, 3, 5, 7, 8, 13};

typedef enum { WORDS_DELAY, WORDS_DYNDELAY, WORDS_REVDELAY } words_kind;

struct gm2_firmware {
  gm2_emu *emu;
  const gm2_effect *effect;
  uint8_t index[DELAY_PARAM_WORDS]; /* descriptor index per knob */
  uint32_t callback[DELAY_PARAM_WORDS];
  int32_t raw[DELAY_PARAM_WORDS]; /* the knob vector, by descriptor index */
  unsigned subtype;
  words_kind words;
  unsigned phase;  /* frame within the four-frame chunk */
  unsigned tick;   /* chunks since the last timed work */
  int32_t grain_ms; /* Y+26 as the previous frame saw it */
  int audio_dirty;
};

static int delay_call(gm2_firmware *self, uint32_t entry, uint32_t a1, uint32_t r0) {
  gm2_set_reg(self->emu, DSP56300_REG_A2, 0);
  gm2_set_reg(self->emu, DSP56300_REG_A1, a1);
  gm2_set_reg(self->emu, DSP56300_REG_A0, 0);
  gm2_set_reg(self->emu, DSP56300_REG_R0, r0);
  gm2_set_reg(self->emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  if (gm2_call(self->emu, entry, DELAY_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "delay: P:0x%06X did not return\n", entry);
    return -1;
  }
  return 0;
}

/* P:0x09423F once, the way the block's dispatcher P:0x094164 runs it: A is the
 * block id, X:(UI+1) the preset-change flag. */
static int delay_activate(gm2_firmware *self, uint32_t pass) {
  gm2_write(self->emu, GM2_X, DELAY_UI + 1u, pass);
  gm2_set_reg(self->emu, DSP56300_REG_A2, 0);
  gm2_set_reg(self->emu, DSP56300_REG_A1, DELAY_ALGORITHM);
  gm2_set_reg(self->emu, DSP56300_REG_A0, 0);
  gm2_set_reg(self->emu, DSP56300_REG_R0, DELAY_UI);
  gm2_set_reg(self->emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  if (gm2_call(self->emu, DELAY_ACTIVATION, 4 * DELAY_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "delay: the activation P:0x%06X did not return\n", DELAY_ACTIVATION);
    return -1;
  }
  return 0;
}

static uint32_t delay_table(const gm2_firmware *self) {
  uint32_t record = gm2_read(self->emu, GM2_X, GM2_CHAIN + DELAY_ALGORITHM);
  return record == 0u ? 0u : gm2_read(self->emu, GM2_P, record + 7u);
}

/* The callback behind every knob, from the block's own descriptor table: a
 * firmware whose table moved cannot be recorded against this effect. */
static int delay_descriptor(gm2_firmware *self) {
  uint32_t record = gm2_read(self->emu, GM2_X, GM2_CHAIN + DELAY_ALGORITHM);
  uint32_t count = gm2_read(self->emu, GM2_P, record + 1u);
  uint32_t table = delay_table(self);
  if (record == 0u || count < DELAY_DESCRIPTOR_WORDS) {
    fprintf(stderr, "delay: descriptor X:0x%06X has %u parameters\n", record, count);
    return -1;
  }
  for (unsigned knob = 0; knob < self->effect->knobs; knob++) {
    uint32_t address = 0;
    unsigned index = self->index[knob];
    if (gm2_firmware_knob_entry(self->emu, table, index, &self->effect->knob[knob], &address) !=
        0) {
      return -1;
    }
    self->callback[index] = address;
  }
  return 0;
}

/* The descriptor's format word decides whether the parameter path runs the
 * callback a second time: bits 23 and 13 set means it does, with A = 0x50 and
 * the value left in X:0x09860C. */
static int delay_second_call(const gm2_firmware *self, unsigned index) {
  uint32_t format = gm2_read(self->emu, GM2_P, delay_table(self) + 15u * index + 12u);
  return (format & 0x800000u) != 0u && (format & 0x2000u) != 0u;
}

/* The device's whole parameter edit P:0x083268 for one knob: the value, the
 * parameter's own callback, its second call, and the raw words of the other knobs
 * put back the way the knob vector has them. */
static int delay_edit(gm2_firmware *self, unsigned index, int32_t value) {
  self->raw[index] = value;
  gm2_write(self->emu, GM2_X, DELAY_PARAMS + index, gm2_word24(value));
  gm2_set_reg(self->emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  gm2_set_reg(self->emu, DSP56300_REG_A2, 0);
  gm2_set_reg(self->emu, DSP56300_REG_A1, DELAY_ALGORITHM);
  gm2_set_reg(self->emu, DSP56300_REG_A0, 0);
  if (gm2_call(self->emu, self->callback[index], DELAY_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "delay: callback P:0x%06X did not return\n", self->callback[index]);
    return -1;
  }
  if (delay_second_call(self, index) != 0) {
    gm2_write(self->emu, GM2_X, DELAY_VALUE_WORD, gm2_word24(value));
    gm2_set_reg(self->emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
    gm2_set_reg(self->emu, DSP56300_REG_A2, 0);
    gm2_set_reg(self->emu, DSP56300_REG_A1, 0x50u);
    gm2_set_reg(self->emu, DSP56300_REG_A0, 0);
    if (gm2_call(self->emu, self->callback[index], DELAY_BUDGET, NULL) != GM2_CALL_OK) {
      fprintf(stderr, "delay: callback P:0x%06X did not return\n", self->callback[index]);
      return -1;
    }
  }
  for (unsigned word = 1u; word < DELAY_PARAM_WORDS; word++) {
    if (word != index) {
      gm2_write(self->emu, GM2_X, DELAY_PARAMS + word, gm2_word24(self->raw[word]));
    }
  }
  /* The callback calls clobber the registers the continuation runs with. */
  self->audio_dirty = 1;
  return 0;
}

static void delay_audio_begin(gm2_firmware *self) { gm2_firmware_audio_reset(self->emu); }

/* The block's job records are these nine, in this order: each engine's
 * coefficient smoother, its six dead words and its two tap crossfades, then the
 * level words' smoother. A firmware whose list moved cannot be recorded against
 * these effects. */
static const struct {
  uint32_t record;
  uint32_t params;
  uint32_t handler;
} k_jobs[] = {
    {0x098E39u, 0x098E15u, GM2_JOB_SMOOTH},    /* engine 0, X:0x030B -> Y:0x0506 */
    {0x098E79u, 0x098E62u, DELAY_TAP_ENTRY},    /* engine 1, channel 1 taps */
    {0x098E75u, 0x098E57u, DELAY_TAP_ENTRY},    /* engine 1, channel 0 taps */
    {0x098E71u, 0x098E4Fu, GM2_JOB_GROUP},     /* engine 1, the dead words */
    {0x098E6Du, 0x098E49u, GM2_JOB_SMOOTH},   /* engine 1, X:0x0341 -> Y:0x0530 */
    {0x098E45u, 0x098E2Eu, DELAY_TAP_ENTRY},    /* engine 0, channel 1 taps */
    {0x098E41u, 0x098E23u, DELAY_TAP_ENTRY},    /* engine 0, channel 0 taps */
    {0x098E3Du, 0x098E1Bu, GM2_JOB_GROUP},     /* engine 0, the dead words */
    {0x098EC5u, 0x098EC9u, GM2_JOB_SMOOTH},    /* the level words -> Y:0x055A */
};

#define DELAY_JOBS (sizeof k_jobs / sizeof k_jobs[0])

/* Channel `ch`'s tap job record, X:0x098E23 or X:0x098E2E: the activation pair
 * leaves the block on the engine at X:0x030B / Y:0x0506. */
static uint32_t delay_tap_job(unsigned ch) { return DELAY_TAP_JOB0 + DELAY_TAP_JOB_STRIDE * ch; }

/* The job list the firmware holds for the block, read and checked against
 * `k_jobs`: the adapter runs every record it holds and nothing else. */
static int delay_jobs(const gm2_firmware *self, uint32_t *record) {
  unsigned count = gm2_firmware_jobs(self->emu, record, GM2_FIRMWARE_JOBS_MAX);
  if (count != (unsigned)DELAY_JOBS) {
    fprintf(stderr, "delay: the block holds %u job records, not %u\n", count, (unsigned)DELAY_JOBS);
    return -1;
  }
  for (unsigned i = 0; i < count; i++) {
    const uint32_t params = gm2_read(self->emu, GM2_X, record[i] + GM2_JOB_PARAMS);
    const uint32_t handler = gm2_read(self->emu, GM2_X, record[i] + GM2_JOB_HANDLER);
    if (record[i] != k_jobs[i].record || params != k_jobs[i].params ||
        handler != k_jobs[i].handler) {
      fprintf(stderr,
              "delay: job %u is X:0x%06X running P:0x%06X on X:0x%06X, not X:0x%06X "
              "running P:0x%06X on X:0x%06X\n",
              i, record[i], handler, params, k_jobs[i].record, k_jobs[i].handler, k_jobs[i].params);
      return -1;
    }
  }
  return 0;
}

/* Every record the firmware holds, run in list order as the manager P:0x0960C6
 * hands each to the runner P:0x0001CD: the record's settled check, then its
 * handler. */
static int delay_run_jobs(gm2_firmware *self) {
  uint32_t record[GM2_FIRMWARE_JOBS_MAX];
  if (delay_jobs(self, record) != 0) {
    return -1;
  }
  for (unsigned i = 0; i < (unsigned)DELAY_JOBS; i++) {
    if (gm2_firmware_run_job(self->emu, record[i]) != 0) {
      return -1;
    }
  }
  return 0;
}

/* The words the block's jobs move: the active engine, the level words, and the
 * live tap offsets, gains and crossfade words. */
static void delay_live_words(const gm2_firmware *self, uint32_t *word) {
  unsigned n = 0;
  for (uint32_t at = DELAY_COEF; at < DELAY_COEF + DELAY_ENGINE_WORDS; at++) {
    word[n++] = gm2_read(self->emu, GM2_Y, at);
  }
  for (uint32_t at = DELAY_LEVEL; at < DELAY_LEVEL + DELAY_LEVEL_WORDS; at++) {
    word[n++] = gm2_read(self->emu, GM2_Y, at);
  }
  for (uint32_t at = DELAY_ENGINE + 33u; at < DELAY_ENGINE + 41u; at++) {
    word[n++] = gm2_read(self->emu, GM2_X, at);
  }
  for (uint32_t at = DELAY_TAP_JOB0; at < DELAY_TAP_JOB0 + 2u * DELAY_TAP_JOB_STRIDE; at++) {
    word[n++] = gm2_read(self->emu, GM2_X, at);
  }
}

/* The rest state: every word those jobs move stops moving. */
static int delay_settle(gm2_firmware *self) {
  uint32_t before[DELAY_SETTLE_WORDS];
  uint32_t after[DELAY_SETTLE_WORDS];
  for (unsigned round = 0; round < DELAY_SETTLE_ROUNDS; round++) {
    delay_live_words(self, before);
    if (delay_run_jobs(self) != 0) {
      return -1;
    }
    delay_live_words(self, after);
    unsigned same = 0;
    for (unsigned i = 0; i < DELAY_SETTLE_WORDS; i++) {
      same += after[i] == before[i] ? 1u : 0u;
    }
    if (same == DELAY_SETTLE_WORDS) {
      return 0;
    }
  }
  fprintf(stderr, "delay: the block's jobs never settled\n");
  return -1;
}

/* The ducking envelope P:0x0944BE in silence, until X+30 stops moving. */
static int delay_settle_duck(gm2_firmware *self) {
  int32_t before = 0;
  gm2_write(self->emu, GM2_X, DELAY_DUCK_LEVEL, 0);
  for (unsigned pass = 0; pass < DELAY_SETTLE_LIMIT; pass++) {
    if (delay_call(self, DELAY_DUCK_TICK, 0u, DELAY_UI) != 0) {
      return -1;
    }
    int32_t duck = gm2_sign24(gm2_read(self->emu, GM2_X, DELAY_DUCK_STATE));
    if (duck == before && pass != 0u) {
      return 0;
    }
    before = duck;
  }
  fprintf(stderr, "delay: the ducking envelope never settled\n");
  return -1;
}

/* The Gate/EQ block's level detector as the old renderers built it: its
 * constants through P:0x090441, its level words from the block's own activation,
 * and the task record whose output buffer nobody reads. */
static int delay_gate(gm2_firmware *self) {
  uint32_t level_addr = 0;
  uint32_t levels[3];
  if (gm2_block_levels(GM2_FIRMWARE_DATA_DIR, GM2_EFFECT_RATE, GATE_ALGORITHM, GATE_UI,
                       GATE_ACTIVATION, &level_addr, levels) != 0) {
    return -1;
  }
  if (level_addr != GATE_LEVEL) {
    fprintf(stderr, "delay: the gate level code targets Y:0x%06X, not Y:0x%06X\n", level_addr,
            GATE_LEVEL);
    return -1;
  }
  gm2_write(self->emu, GM2_X, GATE_UI, GATE_ALGORITHM);
  gm2_write(self->emu, GM2_X, GATE_UI + 4u, 1u);
  gm2_write(self->emu, GM2_X, GATE_UI + 9u, 0u);
  if (gm2_mirror_descriptor(self->emu, GATE_ALGORITHM) < 0 ||
      delay_call(self, GATE_LINK, 0u, GATE_UI) != 0) {
    return -1;
  }
  gm2_write_run(self->emu, GM2_Y, GATE_LEVEL, levels, 3);
  for (uint32_t word = 0; word < GATE_STATE_WORDS; word++) {
    gm2_write(self->emu, GM2_X, GATE_STATE + word, 0);
  }
  gm2_write(self->emu, GM2_Y, GATE_CONFIG, 0);
  gm2_write(self->emu, GM2_Y, GATE_CONFIG + 1u, 0);
  gm2_write(self->emu, GM2_Y, GATE_PEAK, 0x800000u);
  const uint32_t record[7] = {DELAY_INPUT,    GATE_LEVEL,  GATE_STATE,      GATE_CONFIG,
                              GATE_LEVEL + 1, GATE_OUTPUT, GATE_LINK_RECORD};
  gm2_write_run(self->emu, GM2_Y, GATE_TASK, record, 7);
  const uint32_t link[2] = {GM2_STUB_ADDR + 2u, GATE_OUTPUT};
  gm2_write_run(self->emu, GM2_Y, GATE_LINK_RECORD, link, 2);
  return 0;
}

static void delay_close(gm2_firmware *self) {
  if (self != NULL) {
    gm2_destroy(self->emu);
    free(self);
  }
}

static gm2_firmware *delay_open(const gm2_effect *effect, unsigned subtype, const uint8_t *index,
                                unsigned count, words_kind words) {
  if (count != effect->knobs || count > DELAY_PARAM_WORDS) {
    fprintf(stderr, "%s: %u descriptor indices for %u knobs, at most %u\n", effect->name, count,
            effect->knobs, DELAY_PARAM_WORDS);
    return NULL;
  }
  gm2_firmware *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->effect = effect;
  self->subtype = subtype;
  self->words = words;
  for (unsigned knob = 0; knob < count; knob++) {
    self->index[knob] = index[knob];
  }
  for (unsigned word = 0; word < DELAY_PARAM_WORDS; word++) {
    self->raw[word] = 0;
  }
  self->raw[0] = (int32_t)subtype;
  for (unsigned knob = 0; knob < count; knob++) {
    self->raw[index[knob]] = effect->knob[knob].def;
  }

  self->emu = gm2_firmware_boot(effect->name);
  if (self->emu == NULL) {
    delay_close(self);
    return NULL;
  }
  delay_audio_begin(self);
  if (gm2_delay_subtype(self->emu, subtype) != 0) {
    delay_close(self);
    return NULL;
  }
  for (unsigned word = 0; word < DELAY_PARAM_WORDS; word++) {
    gm2_write(self->emu, GM2_X, DELAY_PARAMS + word, gm2_word24(self->raw[word]));
  }
  /* Tap master = Global, so P:0x094735 takes its Tempo branch when Tempo is set
   * and falls back to Dly when it is Ignore. */
  gm2_write(self->emu, GM2_X, DELAY_TAP_MASTER, 1u);
  gm2_write(self->emu, GM2_X, DELAY_BEAT_WORD, GM2_EFFECT_BEAT_MS);
  gm2_write(self->emu, GM2_X, DELAY_TAP_TIME, GM2_EFFECT_BEAT_MS);
  /* The engine builder P:0x094336 takes the block's input, output and link
   * pointers from X:0x098EB8.. on the device; they take this harness's. */
  const uint32_t points[3] = {DELAY_INPUT, DELAY_OUTPUT, DELAY_LINK_RECORD};
  gm2_write_run(self->emu, GM2_X, DELAY_UI_POINTERS, points, 3);
  gm2_reset_regs(self->emu);
  gm2_set_reg(self->emu, DSP56300_REG_R0, DELAY_UI);
  gm2_set_reg(self->emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  if (gm2_call(self->emu, DELAY_ENGINE_BUILDER, DELAY_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "delay: the engine builder P:0x%06X did not return\n", DELAY_ENGINE_BUILDER);
    delay_close(self);
    return NULL;
  }
  /* The preset change and the update that follows it: the pair toggles the engine
   * swap word twice, so the block ends on the engine at X:0x030B. */
  if (delay_activate(self, 1u) != 0 || delay_activate(self, 0u) != 0 ||
      delay_descriptor(self) != 0) {
    delay_close(self);
    return NULL;
  }
  /* The task record's last word names the block's link record; on the device the
   * chain continues with the next block, so its first word is this harness's
   * stop and its second the output the chain would read. */
  const uint32_t link[2] = {GM2_STUB_ADDR + 2u, DELAY_OUTPUT};
  gm2_write_run(self->emu, GM2_Y, DELAY_LINK_RECORD, link, 2);
  for (unsigned word = 0; word < DELAY_PARAM_WORDS; word++) {
    gm2_write(self->emu, GM2_X, DELAY_PARAMS + word, gm2_word24(self->raw[word]));
  }
  for (uint32_t word = 0; word < DELAY_MUTE_WORDS; word++) {
    gm2_write(self->emu, GM2_Y, DELAY_OTHER_COEF + word, 0);
  }
  if (subtype == 1u && delay_gate(self) != 0) {
    delay_close(self);
    return NULL;
  }
  /* The ducking tick is no job: it publishes X+8 and X+9, which the smoother's
   * next pass carries to the live output matrix. */
  if ((subtype == 1u && delay_settle_duck(self) != 0) || delay_settle(self) != 0) {
    delay_close(self);
    return NULL;
  }
  self->grain_ms = gm2_sign24(gm2_read(self->emu, GM2_Y, DELAY_GRAIN_WORD));
  delay_audio_begin(self);
  self->audio_dirty = 0;
  return self;
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int delay_levels(gm2_firmware *self, int32_t level[3]) {
  for (unsigned word = 0; word < 3u; word++) {
    level[word] = gm2_sign24(gm2_read(self->emu, GM2_Y, DELAY_LEVEL + word));
  }
  return 0;
}

/* The knobs whose callback is P:0x094735 itself or its two Tempo entries: only
 * their turns stage read offsets, so only they can leave the pair the edit
 * order's. */
static int delay_moves_offsets(const gm2_firmware *self, unsigned index) {
  const uint32_t callback = self->callback[index];
  return callback == DELAY_DELAY_CALLBACK || callback == DELAY_TEMPO_CALLBACK ||
         callback == DELAY_TEMPO2_CALLBACK;
}

/* P:0x094735 with a non-zero A, the call the block's activation and its message 15
 * make: both channels' read offsets rebuilt from every knob value and the rounded
 * Tempo time written back over the Delay slot. A Delay turn at a Tempo value takes
 * P:0x094735's Ignore path instead, which leaves the raw Delay time staged, so the
 * block's state after that turn is the edit order's rather than the vector's. */
static int delay_tempo_path(gm2_firmware *self) {
  if (delay_call(self, DELAY_TEMPO_ENTRY, 1u, DELAY_UI) != 0) {
    return -1;
  }
  for (unsigned word = 0; word < DELAY_PARAM_WORDS; word++) {
    gm2_write(self->emu, GM2_X, DELAY_PARAMS + word, gm2_word24(self->raw[word]));
  }
  self->audio_dirty = 1;
  return 0;
}

static int delay_set_knob(gm2_firmware *self, unsigned knob, int value) {
  uint32_t record[GM2_FIRMWARE_JOBS_MAX];
  const unsigned index = self->index[knob];
  if (delay_edit(self, index, (int32_t)value) != 0) {
    return -1;
  }
  if (delay_moves_offsets(self, index) != 0 && delay_tempo_path(self) != 0) {
    return -1;
  }
  return delay_jobs(self, record);
}

static void delay_coef_words(const gm2_firmware *self, int32_t *words, unsigned *count) {
  for (unsigned word = 0; word < DELAY_COEF_WORDS; word++) {
    if (self->words == WORDS_DYNDELAY && (word == 8u || word == 9u)) {
      continue;
    }
    words[(*count)++] = gm2_sign24(gm2_read(self->emu, GM2_X, DELAY_ENGINE + word));
  }
}

static int delay_words(gm2_firmware *self, int32_t *words) {
  unsigned count = 0;
  delay_coef_words(self, words, &count);
  if (self->words == WORDS_DYNDELAY) {
    for (unsigned word = 0; word < 4u; word++) {
      words[count++] = gm2_sign24(gm2_read(self->emu, GM2_X, DELAY_DUCK_WORDS + word));
    }
  }
  if (self->words == WORDS_REVDELAY) {
    words[count++] = gm2_sign24(gm2_read(self->emu, GM2_Y, DELAY_GRAIN_WORD));
  } else {
    for (unsigned ch = 0; ch < 2u; ch++) {
      words[count++] =
          gm2_sign24(gm2_read(self->emu, GM2_X, delay_tap_job(ch) + DELAY_TAP_STAGED_WORD));
    }
  }
  for (uint32_t at = DELAY_LEVEL_STAGED + 1u; at < DELAY_LEVEL_STAGED + DELAY_LEVEL_WORDS; at++) {
    words[count++] = gm2_sign24(gm2_read(self->emu, GM2_X, at));
  }
  return count == self->effect->setup_words ? 0 : -1;
}

/* The timed work a chunk answers: the ducking tick every gm2_tick_chunks()
 * chunks on the Dynamic Delay, the grain splice every gm2_tick_period() on the
 * Reverse Delay, held back while a crossfade still runs. */
static int delay_timed(gm2_firmware *self) {
  self->tick++;
  if (self->subtype == 1u) {
    if (self->tick < gm2_tick_chunks(GM2_EFFECT_RATE)) {
      return 0;
    }
    self->tick = 0;
    if (delay_call(self, GATE_READ, 0u, GATE_UI) != 0) {
      return -1;
    }
    gm2_write(self->emu, GM2_X, DELAY_DUCK_LEVEL,
              gm2_word24(gm2_sign24(gm2_get_reg(self->emu, DSP56300_REG_A1))));
    return delay_call(self, DELAY_DUCK_TICK, 0u, DELAY_UI);
  }
  if (self->subtype != 3u) {
    self->tick = 0;
    return 0;
  }
  if (self->tick < gm2_tick_period(GM2_EFFECT_RATE, (unsigned)self->grain_ms)) {
    return 0;
  }
  for (unsigned ch = 0; ch < 2u; ch++) {
    if (gm2_read(self->emu, GM2_X, delay_tap_job(ch) + DELAY_TAP_STEP_WORD) != 0u) {
      return 0;
    }
  }
  self->tick = 0;
  return delay_call(self, DELAY_SPLICE, 0u, DELAY_UI);
}

static int delay_frame(gm2_firmware *self, int32_t left, int32_t right, int32_t *out_left,
                       int32_t *out_right) {
  if (self->audio_dirty != 0) {
    delay_audio_begin(self);
    self->audio_dirty = 0;
  }
  gm2_write(self->emu, GM2_X, DELAY_INPUT, gm2_word24(left));
  gm2_write(self->emu, GM2_X, DELAY_INPUT + 1u, gm2_word24(right));
  if (self->subtype == 3u) {
    /* P:0x094735 re-anchors the grain timer target Y+27 to the 1 kHz count, so a
     * Delay or Tempo turn restarts the period from the frame that follows it. */
    int32_t delay_ms = gm2_sign24(gm2_read(self->emu, GM2_Y, DELAY_GRAIN_WORD));
    if (delay_ms != self->grain_ms) {
      self->grain_ms = delay_ms;
      self->tick = 0u;
    }
  }
  if (self->subtype == 1u) {
    gm2_write(self->emu, GM2_X, DELAY_CHUNK_PHASE, self->phase == 0u ? 4u : 0u);
    gm2_set_reg(self->emu, DSP56300_REG_R6, GATE_TASK);
    if (gm2_run_until_stop(self->emu, GATE_ENTRY, GATE_BUDGET, NULL) != GM2_CALL_OK) {
      return -1;
    }
  }
  gm2_set_reg(self->emu, DSP56300_REG_R6, DELAY_TASK_ENTRY);
  if (gm2_run_until_stop(self->emu, DELAY_ENTRY, DELAY_BUDGET, NULL) != GM2_CALL_OK) {
    return -1;
  }
  *out_left = gm2_sign24(gm2_read(self->emu, GM2_X, DELAY_OUTPUT));
  *out_right = gm2_sign24(gm2_read(self->emu, GM2_X, DELAY_OUTPUT + 1u));
  self->phase = (self->phase + 1u) % GM2_CHUNK_FRAMES;
  if (self->phase != 0u) {
    return 0;
  }
  if (delay_run_jobs(self) != 0) {
    return -1;
  }
  return delay_timed(self);
}

static gm2_firmware *pingpong_open(void) {
  return delay_open(&gm2_effect_pingpong, 0u, k_pingpong_index,
                    sizeof k_pingpong_index / sizeof k_pingpong_index[0], WORDS_DELAY);
}

static gm2_firmware *dyndelay_open(void) {
  return delay_open(&gm2_effect_dyndelay, 1u, k_dyndelay_index,
                    sizeof k_dyndelay_index / sizeof k_dyndelay_index[0], WORDS_DYNDELAY);
}

static gm2_firmware *dualdelay_open(void) {
  return delay_open(&gm2_effect_dualdelay, 2u, k_dualdelay_index,
                    sizeof k_dualdelay_index / sizeof k_dualdelay_index[0], WORDS_DELAY);
}

static gm2_firmware *revdelay_open(void) {
  return delay_open(&gm2_effect_revdelay, 3u, k_revdelay_index,
                    sizeof k_revdelay_index / sizeof k_revdelay_index[0], WORDS_REVDELAY);
}

const gm2_firmware_ops gm2_firmware_pingpong = {
    .effect = &gm2_effect_pingpong,
    .open = pingpong_open,
    .levels = delay_levels,
    .set_knob = delay_set_knob,
    .words = delay_words,
    .frame = delay_frame,
    .close = delay_close,
};

const gm2_firmware_ops gm2_firmware_dyndelay = {
    .effect = &gm2_effect_dyndelay,
    .open = dyndelay_open,
    .levels = delay_levels,
    .set_knob = delay_set_knob,
    .words = delay_words,
    .frame = delay_frame,
    .close = delay_close,
};

const gm2_firmware_ops gm2_firmware_dualdelay = {
    .effect = &gm2_effect_dualdelay,
    .open = dualdelay_open,
    .levels = delay_levels,
    .set_knob = delay_set_knob,
    .words = delay_words,
    .frame = delay_frame,
    .close = delay_close,
};

const gm2_firmware_ops gm2_firmware_revdelay = {
    .effect = &gm2_effect_revdelay,
    .open = revdelay_open,
    .levels = delay_levels,
    .set_knob = delay_set_knob,
    .words = delay_words,
    .frame = delay_frame,
    .close = delay_close,
};
