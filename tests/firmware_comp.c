/* The compressor as the firmware runs it: the block activation P:0x094C7C with
 * its constants P:0x094CFA, the two descriptor updaters P:0x094D5C/P:0x094E18
 * with the ramp records P:0x09605F arms and the P:0x0001FD jobs that publish the
 * detector and the two coefficient triples, and the audio continuation
 * P:0x00125E with the task record the activation installs.
 */
#include "firmware_common.h"
#include "gm2_comp.h"
#include "gm2_effect.h"
#include "gm2_emu.h"
#include "gm2_record.h"

#include <dsp56300.h>

#include <stdio.h>
#include <stdlib.h>

#define COMP_ALGORITHM 0u
#define COMP_UI 0x098F45u
#define COMP_RAW 0x099B36u /* X:0x099B36 + 16 * 0, raw index 0..4 */
#define COMP_STAGED 0x098F0Bu /* release step, release pair, attack pair, detector, ... */
#define COMP_JOB_DATA 0x098F16u /* the block's records: payloads, then list entries */
#define COMP_JOB_DATA_WORDS 0x48u
#define COMP_LEVEL 0x0578u
#define COMP_ACTIVATION 0x094C7Cu
#define COMP_STACK 0x1000u
#define COMP_AUDIO_ENTRY 0x00125Eu
#define COMP_TASK 0x055Eu
#define COMP_AUDIO_IN 0x0600u
#define COMP_AUDIO_OUT 0x0620u
#define COMP_LINK 0x070Bu
#define COMP_CHUNK_PHASE 0x0001u
#define COMP_AUDIO_BUDGET 400000
#define COMP_DESCRIPTOR_STRIDE 15u
#define COMP_DESCRIPTOR_FORMAT 12u
#define COMP_JOBS_MAX 16u

/* The words the block's records move: threshold, ratio, makeup gain, detector,
 * then the release step with the two coefficient triples. */
static const uint32_t k_targets[] = {0x056Au, 0x056Cu, 0x0575u, 0x0567u, 0x056Du, COMP_LEVEL};

static const uint32_t k_handlers[] = {GM2_JOB_LINEAR, GM2_JOB_SMOOTH};

static const gm2_firmware_job_spec k_jobs = {
    .name = "comp",
    .handlers = k_handlers,
    .handler_count = sizeof k_handlers / sizeof k_handlers[0],
    .data = COMP_JOB_DATA,
    .data_words = COMP_JOB_DATA_WORDS,
    .targets = k_targets,
    .target_count = sizeof k_targets / sizeof k_targets[0],
    .cap = COMP_JOBS_MAX,
};

struct gm2_firmware {
  gm2_emu *emu;
  uint32_t callback[5];
  unsigned phase;
  int audio_dirty;
};

/* The block's own level code on a fresh image: the destination word it targets
 * and the three words it stages. */
static int comp_level_words(uint32_t levels[3]) {
  uint32_t level_addr = 0;
  if (gm2_block_levels(GM2_FIRMWARE_DATA_DIR, GM2_EFFECT_RATE, COMP_ALGORITHM, COMP_UI,
                       COMP_ACTIVATION, &level_addr, levels) != 0) {
    return -1;
  }
  if (level_addr != COMP_LEVEL) {
    fprintf(stderr, "comp: the block level code targets Y:0x%06X, not Y:0x%06X\n", level_addr,
            COMP_LEVEL);
    return -1;
  }
  return 0;
}

/* The block selected as the device selects it. */
static int comp_select(gm2_emu *emu) {
  gm2_write(emu, GM2_X, COMP_UI, COMP_ALGORITHM);
  gm2_write(emu, GM2_X, COMP_UI + 1u, 1);
  gm2_write(emu, GM2_X, COMP_UI + 2u, COMP_AUDIO_IN);
  gm2_write(emu, GM2_X, COMP_UI + 3u, COMP_AUDIO_OUT);
  gm2_write(emu, GM2_X, COMP_UI + 4u, COMP_LINK);
  return gm2_mirror_descriptor(emu, COMP_ALGORITHM) < 0 ? -1 : 0;
}

/* The descriptor's name, clamp window, format word and callback for every knob,
 * against the knob table. The device runs the callback a second time, with
 * A = 0x50 and the value at X:0x09860C, only for a format word with bits 23 and
 * 13 set, and no compressor entry has them. */
static int comp_descriptor(const gm2_emu *emu, uint32_t callback[5]) {
  uint32_t record = gm2_read(emu, GM2_X, GM2_CHAIN + COMP_ALGORITHM);
  uint32_t table = gm2_read(emu, GM2_P, record + 7u);
  if (record == 0u || gm2_read(emu, GM2_P, record + 1u) < gm2_effect_comp.knobs) {
    fprintf(stderr, "comp: descriptor X:0x%06X has %u parameters\n", record,
            gm2_read(emu, GM2_P, record + 1u));
    return -1;
  }
  for (unsigned knob = 0; knob < gm2_effect_comp.knobs; knob++) {
    callback[knob] = 0;
    if (gm2_firmware_knob_entry(emu, table, knob, &gm2_effect_comp.knob[knob], &callback[knob]) !=
        0) {
      return -1;
    }
    uint32_t entry = table + COMP_DESCRIPTOR_STRIDE * knob;
    uint32_t format = gm2_read(emu, GM2_X, entry + COMP_DESCRIPTOR_FORMAT);
    if (gm2_firmware_second_call(format)) {
      fprintf(stderr, "comp: %s asks for the second edit call\n", gm2_effect_comp.knob[knob].name);
      return -1;
    }
  }
  return 0;
}

/* The records the block holds are the ones the port models: three P:0x0001B9
 * ramps P:0x09605F arms, two P:0x0001FD one-poles, over the block's own data, and
 * every word the kernel reads from a record has its record. */
static int comp_check_jobs(const gm2_firmware *self) {
  return gm2_firmware_check_jobs(self->emu, &k_jobs);
}

/* The device's parameter edit for one knob: the raw word, then the descriptor's
 * own updater with the registers P:0x083268 passes, A = the algorithm and B = the
 * value, which stages its words and re-arms its ramps. */
static int comp_set_knob(gm2_firmware *self, unsigned knob, int value) {
  gm2_write(self->emu, GM2_X, COMP_RAW + knob, (uint32_t)value & 0xFFFFFFu);
  if (gm2_firmware_edit(self->emu, COMP_ALGORITHM, self->callback[knob], value, COMP_STACK) != 0) {
    fprintf(stderr, "comp: updater P:0x%06X did not return for knob %u\n", self->callback[knob],
            knob);
    return -1;
  }
  if (comp_check_jobs(self) != 0) {
    return -1;
  }
  /* The updater call clobbers the registers the continuation runs with. */
  self->audio_dirty = 1;
  return 0;
}

static void comp_audio_begin(gm2_emu *emu) {
  gm2_firmware_audio_reset(emu);
  gm2_set_reg(emu, DSP56300_REG_R6, COMP_TASK);
}

static int comp_frame(gm2_firmware *self, int32_t left, int32_t right, int32_t *out_left,
                      int32_t *out_right) {
  if (self->audio_dirty != 0) {
    comp_audio_begin(self->emu);
    /* Only the registers the callback clobbered; the chunk cadence keeps counting. */
    self->audio_dirty = 0;
  }
  gm2_write(self->emu, GM2_X, COMP_AUDIO_IN, gm2_word24(left));
  gm2_write(self->emu, GM2_X, COMP_AUDIO_IN + 1u, gm2_word24(right));
  gm2_write(self->emu, GM2_X, COMP_CHUNK_PHASE, 4u - self->phase);
  gm2_set_reg(self->emu, DSP56300_REG_R6, COMP_TASK);
  if (gm2_run_until_stop(self->emu, COMP_AUDIO_ENTRY, COMP_AUDIO_BUDGET, NULL) != GM2_CALL_OK) {
    return -1;
  }
  *out_left = gm2_sign24(gm2_read(self->emu, GM2_X, COMP_AUDIO_OUT));
  *out_right = gm2_sign24(gm2_read(self->emu, GM2_X, COMP_AUDIO_OUT + 1u));
  self->phase = (self->phase + 1u) & 3u;
  if (self->phase == 0u) {
    return gm2_firmware_run_jobs(self->emu, "comp", COMP_JOBS_MAX);
  }
  return 0;
}

static void comp_close(gm2_firmware *self) {
  if (self != NULL) {
    gm2_destroy(self->emu);
    free(self);
  }
}

static gm2_firmware *comp_open(void) {
  gm2_firmware *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->emu = gm2_firmware_boot("comp");
  if (self->emu == NULL || comp_select(self->emu) != 0 ||
      comp_descriptor(self->emu, self->callback) != 0) {
    comp_close(self);
    return NULL;
  }
  for (unsigned knob = 0; knob < gm2_effect_comp.knobs; knob++) {
    gm2_write(self->emu, GM2_X, COMP_RAW + knob,
              (uint32_t)gm2_effect_comp.knob[knob].def & 0xFFFFFFu);
  }
  gm2_set_reg(self->emu, DSP56300_REG_A2, 0);
  gm2_set_reg(self->emu, DSP56300_REG_A1, COMP_ALGORITHM);
  gm2_set_reg(self->emu, DSP56300_REG_A0, 0);
  gm2_set_reg(self->emu, DSP56300_REG_R0, COMP_UI);
  gm2_set_reg(self->emu, DSP56300_REG_R7, COMP_STACK);
  if (gm2_call(self->emu, COMP_ACTIVATION, GM2_FIRMWARE_CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "comp: activation P:0x%06X did not return\n", COMP_ACTIVATION);
    comp_close(self);
    return NULL;
  }
  if (comp_check_jobs(self) != 0 ||
      gm2_firmware_settle_words(self->emu, "comp", COMP_JOBS_MAX) != 0) {
    comp_close(self);
    return NULL;
  }
  /* The block's own chain reaches its output through this record. */
  gm2_firmware_link(self->emu, COMP_LINK, COMP_AUDIO_OUT);
  if (gm2_read(self->emu, GM2_Y, COMP_TASK + 8u) != COMP_LINK) {
    fprintf(stderr, "comp: the task record links Y:0x%06X, not Y:0x%06X\n",
            gm2_read(self->emu, GM2_Y, COMP_TASK + 8u), COMP_LINK);
    comp_close(self);
    return NULL;
  }
  self->audio_dirty = 1;
  return self;
}

static int comp_levels(gm2_firmware *self, int32_t level[3]) {
  uint32_t words[3];
  (void)self;
  if (comp_level_words(words) != 0) {
    return -1;
  }
  for (unsigned i = 0; i < 3u; i++) {
    level[i] = gm2_sign24(words[i]);
  }
  return 0;
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int comp_words(gm2_firmware *self, int32_t *words) {
  static const uint32_t staged[GM2_COMP_COEFS] = {
      COMP_STAGED + 8u, /* threshold, X:0x098F13 */
      COMP_STAGED + 9u, /* ratio, X:0x098F14 */
      COMP_STAGED + 10u, /* gain, X:0x098F15 */
      COMP_STAGED + 7u, /* detector, X:0x098F12 */
      COMP_STAGED + 0u, /* release step, X:0x098F0B */
      COMP_STAGED + 1u, /* release[0] */
      COMP_STAGED + 2u,  COMP_STAGED + 3u, COMP_STAGED + 4u, /* attack[0], X:0x098F0F */
      COMP_STAGED + 5u,  COMP_STAGED + 6u,
  };
  for (unsigned word = 0; word < GM2_COMP_COEFS; word++) {
    words[word] = gm2_sign24(gm2_read(self->emu, GM2_X, staged[word]));
  }
  return 0;
}

const gm2_firmware_ops gm2_firmware_comp = {
    .effect = &gm2_effect_comp,
    .open = comp_open,
    .levels = comp_levels,
    .set_knob = comp_set_knob,
    .words = comp_words,
    .frame = comp_frame,
    .close = comp_close,
};
