/* The noise gate as the firmware runs it: the Gate/EQ block's activation
 * P:0x09038B, the four descriptor updaters P:0x0904E8/P:0x090555/P:0x090513/
 * P:0x090582 with the ramp records P:0x09605F arms and the P:0x0001FD job that
 * publishes the release words, and the audio continuation P:0x000C47 with its
 * task record.
 */
#include "firmware_common.h"
#include "gm2_effect.h"
#include "gm2_emu.h"
#include "gm2_gate.h"
#include "gm2_record.h"

#include <dsp56300.h>

#include <stdio.h>
#include <stdlib.h>

#define GATE_ALGORITHM 6u
#define GATE_UI 0x0987A4u
#define GATE_PARAMS 0x099B36u
#define GATE_PARAM_STRIDE 16u
#define GATE_FIRST_KNOB 0u
#define GATE_RAW (GATE_PARAMS + GATE_ALGORITHM * GATE_PARAM_STRIDE)
#define GATE_ACTIVE_INDEX 4u
#define GATE_STAGED 0x098745u /* release[0..3], threshold, mode, damping */
#define GATE_JOB_DATA 0x09874Cu /* the block's records: payloads and list entries */
#define GATE_JOB_DATA_WORDS 0x74u
#define GATE_LEVEL 0x0394u
#define GATE_ACTIVATION 0x09038Bu
#define GATE_STACK 0x1000u
#define GATE_AUDIO_ENTRY 0x000C47u
#define GATE_TASK 0x0700u
#define GATE_AUDIO_IN 0x0600u
#define GATE_AUDIO_OUT 0x0620u
#define GATE_AUDIO_STATE 0x02A0u
#define GATE_AUDIO_CONFIG 0x0371u
#define GATE_AUDIO_WET 0x0395u
#define GATE_CHUNK_PHASE 0x0001u
#define GATE_AUDIO_BUDGET 400000
#define GATE_DESCRIPTOR_STRIDE 15u
#define GATE_DESCRIPTOR_FORMAT 12u
#define GATE_JOBS_MAX 16u

/* The words the block's records move: the gate's four parameters and the level
 * job's three, then the three EQ bands' coefficients, which the block queues
 * whether or not the EQ is active. */
static const uint32_t k_targets[] = {0x0376u, 0x0379u,    0x037Au, 0x037Bu, 0x037Cu, 0x037Du,
                                     0x037Eu, GATE_LEVEL, 0x0385u, 0x038Au, 0x038Fu};

static const uint32_t k_handlers[] = {GM2_JOB_LINEAR, GM2_JOB_SMOOTH, GM2_JOB_GROUP};

static const gm2_firmware_job_spec k_jobs = {
    .name = "gate",
    .handlers = k_handlers,
    .handler_count = sizeof k_handlers / sizeof k_handlers[0],
    .data = GATE_JOB_DATA,
    .data_words = GATE_JOB_DATA_WORDS,
    .targets = k_targets,
    .target_count = sizeof k_targets / sizeof k_targets[0],
    .cap = GATE_JOBS_MAX,
};

struct gm2_firmware {
  gm2_emu *emu;
  uint32_t callback[4];
  unsigned phase;
  int audio_dirty;
};

/* The block's own level code on a fresh image: the destination word it targets
 * and the three words it stages. */
static int gate_level_words(uint32_t levels[3]) {
  uint32_t level_addr = 0;
  if (gm2_block_levels(GM2_FIRMWARE_DATA_DIR, GM2_EFFECT_RATE, GATE_ALGORITHM, GATE_UI,
                       GATE_ACTIVATION, &level_addr, levels) != 0) {
    return -1;
  }
  if (level_addr != GATE_LEVEL) {
    fprintf(stderr, "gate: the block level code targets Y:0x%06X, not Y:0x%06X\n", level_addr,
            GATE_LEVEL);
    return -1;
  }
  return 0;
}

/* The block selected as the device selects it, with the EQ bands inactive: only
 * the gate's four parameters are on the fixture's pages. */
static int gate_select(gm2_emu *emu) {
  gm2_write(emu, GM2_X, GATE_UI, GATE_ALGORITHM);
  gm2_write(emu, GM2_X, GATE_UI + 4u, 1);
  gm2_write(emu, GM2_X, GATE_UI + 9u, 0);
  gm2_write(emu, GM2_X, GATE_RAW + GATE_ACTIVE_INDEX, 1);
  return gm2_mirror_descriptor(emu, GATE_ALGORITHM) < 0 ? -1 : 0;
}

/* The descriptor's name, clamp window, format word and callback for every knob,
 * against the knob table: a firmware whose table moved cannot be recorded against
 * this effect. The device runs the callback a second time, with A = 0x50 and the
 * value at X:0x09860C, only for a format word with bits 23 and 13 set, and no gate
 * entry has them. */
static int gate_descriptor(const gm2_emu *emu, uint32_t callback[4]) {
  uint32_t record = gm2_read(emu, GM2_X, GM2_CHAIN + GATE_ALGORITHM);
  uint32_t table = gm2_read(emu, GM2_P, record + 7u);
  if (record == 0u || gm2_read(emu, GM2_P, record + 1u) < GATE_FIRST_KNOB + gm2_effect_gate.knobs) {
    fprintf(stderr, "gate: descriptor X:0x%06X has %u parameters\n", record,
            gm2_read(emu, GM2_P, record + 1u));
    return -1;
  }
  for (unsigned knob = 0; knob < gm2_effect_gate.knobs; knob++) {
    callback[knob] = 0;
    if (gm2_firmware_knob_entry(emu, table, GATE_FIRST_KNOB + knob, &gm2_effect_gate.knob[knob],
                                &callback[knob]) != 0) {
      return -1;
    }
    uint32_t entry = table + GATE_DESCRIPTOR_STRIDE * (GATE_FIRST_KNOB + knob);
    uint32_t format = gm2_read(emu, GM2_X, entry + GATE_DESCRIPTOR_FORMAT);
    if (gm2_firmware_second_call(format)) {
      fprintf(stderr, "gate: %s asks for the second edit call\n", gm2_effect_gate.knob[knob].name);
      return -1;
    }
  }
  return 0;
}

/* The records the block holds are the ones the port models: every record a
 * P:0x0001B9 ramp P:0x09605F arms, a P:0x0001FD one-pole, or one of the EQ bands'
 * P:0x00026F groups, over the block's own data, and every word the gate and the
 * level job move has its record. */
static int gate_check_jobs(const gm2_firmware *self) {
  return gm2_firmware_check_jobs(self->emu, &k_jobs);
}

/* The whole device parameter edit for one knob: the raw word, then the
 * descriptor's own updater with the registers P:0x083268 passes, A = the
 * algorithm and B = the value, which stages its words and re-arms its ramps. */
static int gate_set_knob(gm2_firmware *self, unsigned knob, int value) {
  gm2_write(self->emu, GM2_X, GATE_RAW + GATE_FIRST_KNOB + knob, (uint32_t)value & 0xFFFFFFu);
  if (gm2_firmware_edit(self->emu, GATE_ALGORITHM, self->callback[knob], value, GATE_STACK) != 0) {
    fprintf(stderr, "gate: updater P:0x%06X did not return for knob %u\n", self->callback[knob],
            knob);
    return -1;
  }
  if (gate_check_jobs(self) != 0) {
    return -1;
  }
  /* The updater call clobbers the registers the continuation runs with. */
  self->audio_dirty = 1;
  return 0;
}

/* The continuation's record: input, gain, state, config, wet/dry, output, then
 * the link record {stop address, output pointer}. */
static void gate_task_record(gm2_emu *emu) {
  const uint32_t record[7] = {GATE_AUDIO_IN,  GATE_LEVEL,     GATE_AUDIO_STATE, GATE_AUDIO_CONFIG,
                              GATE_AUDIO_WET, GATE_AUDIO_OUT, GATE_TASK + 8u};
  gm2_write_run(emu, GM2_Y, GATE_TASK, record, 7);
  gm2_firmware_link(emu, GATE_TASK + 8u, GATE_AUDIO_OUT);
}

static void gate_audio_begin(gm2_emu *emu) {
  gm2_firmware_audio_reset(emu);
  gm2_set_reg(emu, DSP56300_REG_R6, GATE_TASK);
  gate_task_record(emu);
}

/* One frame of the continuation, then the block's jobs once per four-frame
 * chunk. */
static int gate_frame(gm2_firmware *self, int32_t left, int32_t right, int32_t *out_left,
                      int32_t *out_right) {
  if (self->audio_dirty != 0) {
    gate_audio_begin(self->emu);
    /* Only the registers the callback clobbered; the chunk cadence keeps counting. */
    self->audio_dirty = 0;
  }
  gm2_write(self->emu, GM2_X, GATE_AUDIO_IN, gm2_word24(left));
  gm2_write(self->emu, GM2_X, GATE_AUDIO_IN + 1u, gm2_word24(right));
  gm2_write(self->emu, GM2_X, GATE_CHUNK_PHASE, 4u - self->phase);
  gm2_set_reg(self->emu, DSP56300_REG_R6, GATE_TASK);
  if (gm2_run_until_stop(self->emu, GATE_AUDIO_ENTRY, GATE_AUDIO_BUDGET, NULL) != GM2_CALL_OK) {
    return -1;
  }
  *out_left = gm2_sign24(gm2_read(self->emu, GM2_X, GATE_AUDIO_OUT));
  *out_right = gm2_sign24(gm2_read(self->emu, GM2_X, GATE_AUDIO_OUT + 1u));
  self->phase = (self->phase + 1u) & 3u;
  if (self->phase == 0u) {
    return gm2_firmware_run_jobs(self->emu, "gate", GATE_JOBS_MAX);
  }
  return 0;
}

GM2_FIRMWARE_CLOSE(gate_close)

static gm2_firmware *gate_open(void) {
  gm2_firmware *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->emu = gm2_firmware_boot("gate");
  if (self->emu == NULL || gate_select(self->emu) != 0 ||
      gate_descriptor(self->emu, self->callback) != 0) {
    gate_close(self);
    return NULL;
  }
  for (unsigned knob = 0; knob < gm2_effect_gate.knobs; knob++) {
    gm2_write(self->emu, GM2_X, GATE_RAW + GATE_FIRST_KNOB + knob,
              (uint32_t)gm2_effect_gate.knob[knob].def & 0xFFFFFFu);
  }
  gm2_set_reg(self->emu, DSP56300_REG_A2, 0);
  gm2_set_reg(self->emu, DSP56300_REG_A1, GATE_ALGORITHM);
  gm2_set_reg(self->emu, DSP56300_REG_A0, 0);
  gm2_set_reg(self->emu, DSP56300_REG_R0, GATE_UI);
  gm2_set_reg(self->emu, DSP56300_REG_R7, GATE_STACK);
  if (gm2_call(self->emu, GATE_ACTIVATION, GM2_FIRMWARE_CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "gate: activation P:0x%06X did not return\n", GATE_ACTIVATION);
    gate_close(self);
    return NULL;
  }
  if (gate_check_jobs(self) != 0 ||
      gm2_firmware_settle_words(self->emu, "gate", GATE_JOBS_MAX) != 0) {
    gate_close(self);
    return NULL;
  }
  self->audio_dirty = 1;
  return self;
}

static int gate_levels(gm2_firmware *self, int32_t level[3]) {
  uint32_t words[3];
  (void)self;
  if (gate_level_words(words) != 0) {
    return -1;
  }
  for (unsigned i = 0; i < 3u; i++) {
    level[i] = gm2_sign24(words[i]);
  }
  return 0;
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int gate_words(gm2_firmware *self, int32_t *words) {
  static const uint32_t staged[7] = {
      GATE_STAGED + 5u, /* mode */
      GATE_STAGED + 4u, /* threshold */
      GATE_STAGED + 6u, /* damping */
      GATE_STAGED + 0u, /* release[0] */
      GATE_STAGED + 1u, GATE_STAGED + 2u, GATE_STAGED + 3u,
  };
  for (unsigned word = 0; word < 7u; word++) {
    words[word] = gm2_sign24(gm2_read(self->emu, GM2_X, staged[word]));
  }
  return 0;
}

const gm2_firmware_ops gm2_firmware_gate = {
    .effect = &gm2_effect_gate,
    .open = gate_open,
    .levels = gate_levels,
    .set_knob = gate_set_knob,
    .words = gate_words,
    .frame = gate_frame,
    .close = gate_close,
};
