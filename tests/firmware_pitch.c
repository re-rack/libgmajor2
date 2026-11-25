/* The Pitch block as the firmware runs it: the block's own initializer P:0x082B31
 * and ring entry P:0x082B6D, the subtype's descriptor tables, the activation
 * P:0x090937, the device's parameter edit P:0x083268 for a knob turn and the two
 * routines every turn reruns, P:0x090C89 for the ratios and the harmony curves and
 * P:0x090E51 for the feedback pair and the pan gains, and the audio continuation
 * P:0x000D15 with its task record, the detector it calls and the block's four job
 * records.
 */
#include "firmware_common.h"
#include "gm2_effect.h"
#include "gm2_emu.h"
#include "gm2_pitch_core.h"
#include "gm2_record.h"
#include "gm2_tables.h"

#include <dsp56300.h>

#include <stdio.h>
#include <stdlib.h>

#define PITCH_UI 0x0987EAu     /* X: the block's UI object */
#define PITCH_PARAMS 0x099B56u /* X: the block's sixteen raw words */
#define PITCH_RAW_WORDS 16u
#define PITCH_FIRST_PARAM 1u
#define PITCH_LAST_PARAM 12u
#define PITCH_OUT_LEVEL_INDEX 13u
#define PITCH_DETECT_INDEX 14u
#define PITCH_BYPASS_INDEX 15u
#define PITCH_OUT_LEVEL_RAW 0
#define PITCH_ALGORITHM 2u
#define PITCH_TABLE_POINTER 0x098853u /* X: one descriptor table per subtype */
#define PITCH_DESCRIPTOR_DEFAULT 13u  /* the subtype's factory bank word */
#define PITCH_BLOCK_INIT 0x082B31u
#define PITCH_BLOCK_RING 0x082B6Du
#define PITCH_ACTIVATION 0x090937u
#define PITCH_EDIT 0x083268u /* A1 = algorithm<<4 | index, B1 = the value */
#define PITCH_RATIOS 0x090C89u /* the ratios, and the two curves on subtype 4 */
#define PITCH_LEVELS 0x090E51u /* the feedback pair and the four pan gains */
#define PITCH_ENTRY 0x000D15u
#define PITCH_POST_CHUNK 0x0001E4u /* P: the chain's own post-chunk task */
#define PITCH_JOBS 4u
#define PITCH_TRAVEL_ONE 0x000DB1u
#define PITCH_TRAVEL_TWO 0x000DD3u
#define PITCH_STEPS 0x000E33u
#define PITCH_STAGED 0x0987C6u /* X: fb1, fb2, V2_R, V2_L, V1_L, V1_R */
#define PITCH_TASK_RECORD 0x0398u
#define PITCH_TASK_LINK 0x039Eu
#define PITCH_LINK 0x070Bu
#define PITCH_LEVEL 0x03A0u          /* Y: input, wet and dry */
#define PITCH_LEVEL_RECORD 0x098847u /* X: {source, destination, words} */
#define PITCH_LEVEL_STAGED 0x09884Du
#define PITCH_LEVEL_WORDS 3u
#define PITCH_INPUT 0x0600u
#define PITCH_OUTPUT 0x0620u
#define PITCH_CHUNK_WORD 0x0001u
#define PITCH_CHUNK_FRAMES 4u
#define PITCH_AUDIO_BUFFER 0x088000u
#define PITCH_AUDIO_WORDS 0x00004E20u
#define PITCH_DET_BUFFER 0x082000u
#define PITCH_DET_WORDS 0x00000800u
#define PITCH_CHUNK_BUFFER 0x0200u
#define PITCH_CHUNK_WORDS 8u
#define PITCH_FRAME_WORDS 0x03FEu
#define PITCH_FRAME_COUNT 4u
#define PITCH_DELAY_RECORDS 0x0417u
#define PITCH_DELAY_WORDS 6u
#define PITCH_CALL_BUDGET 5000000
#define PITCH_FRAME_BUDGET 20000

/* The four job records the block holds after activation, in list order: the ratio
 * pair, the pan gains, the feedback pair and the level words, each with the
 * payload its parameters record carries. */
typedef struct {
  uint32_t record;
  uint32_t params;
  uint32_t handler;
  int32_t word[5];
} pitch_job;

static const pitch_job k_jobs[PITCH_JOBS] = {
    {0x0987DEu, 0x0987CCu, 0x000212u, {0x041D, 0x0224, 2, 1, 0x020DC6}},
    {0x0987E6u, 0x0987D8u, 0x0001FDu, {0x0987C8, 0x0220, 4, 1, 0x008371}},
    {0x0987E2u, 0x0987D2u, 0x0001FDu, {0x0987C6, 0x021C, 2, 1, 0x008371}},
    {0x098843u, 0x098847u, 0x0001FDu, {0x09884D, 0x03A0, 3, 1, 0x008371}},
};

/* The descriptor entry behind each knob, in the knob table's order. */
static const unsigned k_detune_entry[] = {1u, 2u, 5u, 6u, 12u};
static const unsigned k_whammy_entry[] = {9u, 10u, 11u};
static const unsigned k_octaver_entry[] = {10u, 11u, 12u};
static const unsigned k_pitch_entry[] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 12u};

typedef struct {
  unsigned subtype;
  unsigned knobs;
  const unsigned *entry;
} pitch_block;

struct gm2_firmware {
  gm2_emu *emu;
  const gm2_effect *effect;
  const pitch_block *block;
  unsigned frame;
  int audio_dirty;
};

static void pitch_block_close(gm2_firmware *self) {
  if (self != NULL) {
    gm2_destroy(self->emu);
    free(self);
  }
}

/* One routine of the block, entered as the device's own callers enter it. */
static int pitch_call(gm2_emu *emu, uint32_t entry, uint32_t record) {
  gm2_set_reg(emu, DSP56300_REG_R0, record);
  gm2_set_reg(emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  return gm2_call(emu, entry, PITCH_CALL_BUDGET, NULL) == GM2_CALL_OK ? 0 : -1;
}

/* The travel and step entries end by jumping to the task link instead of with
 * rts, so enter them at their own address like the chunk's epilogue does. */
static int pitch_run(gm2_emu *emu, uint32_t entry) {
  gm2_set_reg(emu, DSP56300_REG_R0, PITCH_TASK_LINK);
  gm2_set_reg(emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  return gm2_run_until_stop(emu, entry, PITCH_CALL_BUDGET, NULL) == GM2_CALL_OK ? 0 : -1;
}

/* The scratch continuation the frame's epilogue jumps to on a chunk's last
 * frame: the chain's own post-chunk task P:0x0001E4, then the harness stop. The
 * other frames reach the stop directly. */
static void pitch_install_continuation(gm2_emu *emu) {
  const uint32_t program[] = {0x0BF080u, PITCH_POST_CHUNK, 0x0AF080u, GM2_STUB_ADDR + 2u};
  gm2_write_run(emu, GM2_P, GM2_SCRATCH_ADDR, program,
                (uint32_t)(sizeof program / sizeof program[0]));
}

/* Every knob's descriptor entry against the knob table and the subtype's factory
 * bank, whose clamped word must be the knob's default. */
static int pitch_descriptor(gm2_firmware *self, uint32_t table) {
  for (unsigned knob = 0; knob < self->block->knobs; knob++) {
    const gm2_knob *record = &self->effect->knob[knob];
    uint32_t callback = 0;
    if (gm2_firmware_knob_entry(self->emu, table, self->block->entry[knob], record, &callback) !=
        0) {
      return -1;
    }
    int32_t bank = gm2_sign24(gm2_read(
        self->emu, GM2_P, table + 15u * self->block->entry[knob] + PITCH_DESCRIPTOR_DEFAULT));
    int32_t clamped = bank;
    if (clamped < record->min) {
      clamped = record->min;
    }
    if (clamped > record->max) {
      clamped = record->max;
    }
    if (clamped != record->def) {
      fprintf(stderr, "%s: the factory bank has %s = %d (%d clamped), the knob table %d\n",
              self->effect->name, record->name, (int)bank, (int)clamped, record->def);
      return -1;
    }
  }
  return 0;
}

/* The device's factory bank, Out Level 0 dB, Detect off and no bypass, then the
 * activation through the descriptor's own words. The Whammy's Mix entry carries
 * no name and its callback pins the word at full, so the bank word serves both. */
static int pitch_raw(gm2_firmware *self, uint32_t table) {
  uint32_t raw[PITCH_RAW_WORDS] = {0};
  raw[0] = self->block->subtype;
  for (unsigned param = PITCH_FIRST_PARAM; param <= PITCH_LAST_PARAM; param++) {
    raw[param] = gm2_read(self->emu, GM2_P, table + 15u * param + PITCH_DESCRIPTOR_DEFAULT);
  }
  raw[PITCH_OUT_LEVEL_INDEX] = PITCH_OUT_LEVEL_RAW;
  raw[PITCH_DETECT_INDEX] = 0u;
  raw[PITCH_BYPASS_INDEX] = 0u;
  gm2_write_run(self->emu, GM2_X, PITCH_PARAMS, raw, PITCH_RAW_WORDS);
  gm2_write(self->emu, GM2_X, PITCH_UI + 1u, 1u);
  gm2_write(self->emu, GM2_X, PITCH_UI + 2u, 0u);
  gm2_reset_regs(self->emu);
  gm2_set_reg(self->emu, DSP56300_REG_A1, PITCH_ALGORITHM);
  gm2_set_reg(self->emu, DSP56300_REG_R0, PITCH_UI);
  gm2_set_reg(self->emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  if (gm2_call(self->emu, PITCH_ACTIVATION, PITCH_CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "%s: the activation P:0x%06X did not return\n", self->effect->name,
            PITCH_ACTIVATION);
    return -1;
  }
  return 0;
}

/* The block's own level code: the record it publishes at X:0x098847 and the three
 * words it stages at X:0x09884D. */
static int pitch_levels_check(const gm2_firmware *self) {
  const uint32_t source = gm2_read(self->emu, GM2_X, PITCH_LEVEL_RECORD);
  const uint32_t destination = gm2_read(self->emu, GM2_X, PITCH_LEVEL_RECORD + 1u);
  const uint32_t words = gm2_read(self->emu, GM2_X, PITCH_LEVEL_RECORD + 2u);
  if (source != PITCH_LEVEL_STAGED || destination != PITCH_LEVEL || words != PITCH_LEVEL_WORDS) {
    fprintf(stderr, "%s: the level record X:0x%06X is {%06X,%06X,%u}\n", self->effect->name,
            PITCH_LEVEL_RECORD, source, destination, words);
    return -1;
  }
  return 0;
}

/* The rest state gm2_pitch_core_state_init models: the travels on their targets,
 * the live words on the staged ones, the level words at their targets, empty
 * buffers and the task record. The travels' bounds and taps come from the
 * firmware's own travel and step entries, entered as the frame's epilogue
 * enters them. */
static int pitch_rest(gm2_firmware *self) {
  gm2_emu *emu = self->emu;
  const uint32_t task[7] = {
      PITCH_INPUT,      /* r3, the stereo input */
      PITCH_INPUT,      /* r3 again, the chunk buffer's source */
      PITCH_LEVEL,      /* r4, the input gain */
      PITCH_LEVEL + 1u, /* r4 for the output stage, the wet gain */
      PITCH_INPUT,      /* r0, the output's dry word */
      PITCH_OUTPUT,     /* r0 for the output stage */
      PITCH_LINK,       /* the entry the epilogue jumps to */
  };
  if (gm2_firmware_settle_words(emu, self->effect->name, PITCH_JOBS) != 0) {
    return -1;
  }
  gm2_write_run(emu, GM2_Y, PITCH_TASK_RECORD, task, 7u);
  gm2_write(emu, GM2_Y, PITCH_LINK, GM2_STUB_ADDR + 2u);
  pitch_install_continuation(emu);
  unsigned pass = 0;
  do {
    if (pitch_run(emu, PITCH_TRAVEL_ONE) != 0 || pitch_run(emu, PITCH_TRAVEL_TWO) != 0) {
      fprintf(stderr, "%s: the travel entry did not return\n", self->effect->name);
      return -1;
    }
  } while (++pass < 512u && (gm2_read(emu, GM2_X, 0x0413u) != gm2_read(emu, GM2_X, 0x0412u) ||
                             gm2_read(emu, GM2_X, 0x0415u) != gm2_read(emu, GM2_X, 0x0414u)));
  if (gm2_read(emu, GM2_X, 0x0413u) != gm2_read(emu, GM2_X, 0x0412u) ||
      gm2_read(emu, GM2_X, 0x0415u) != gm2_read(emu, GM2_X, 0x0414u)) {
    fprintf(stderr, "%s: the voice delay travels did not settle\n", self->effect->name);
    return -1;
  }
  if (pitch_run(emu, PITCH_STEPS) != 0) {
    fprintf(stderr, "%s: the step entry did not return\n", self->effect->name);
    return -1;
  }
  /* The device starts the three buffers and the chunk records empty; the record
   * load leaves preset data at the two buffer windows. */
  uint32_t *x = gm2_raw(emu, GM2_X);
  uint32_t *y = gm2_raw(emu, GM2_Y);
  for (unsigned i = 0; i < PITCH_AUDIO_WORDS; i++) {
    x[PITCH_AUDIO_BUFFER + i] = 0;
  }
  for (unsigned i = 0; i < PITCH_DET_WORDS; i++) {
    x[PITCH_DET_BUFFER + i] = 0;
  }
  for (unsigned i = 0; i < PITCH_CHUNK_WORDS; i++) {
    y[PITCH_CHUNK_BUFFER + i] = 0;
  }
  for (unsigned i = 0; i < PITCH_FRAME_COUNT; i++) {
    x[PITCH_FRAME_WORDS + i] = 0;
  }
  for (unsigned i = 0; i < PITCH_DELAY_WORDS; i++) {
    x[PITCH_DELAY_RECORDS + i] = 0;
  }
  return 0;
}

static int pitch_check_jobs(const gm2_firmware *self);

static gm2_firmware *pitch_block_open(const gm2_effect *effect, const pitch_block *block) {
  gm2_firmware *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->effect = effect;
  self->block = block;
  self->emu = gm2_firmware_boot(effect->name);
  if (self->emu == NULL) {
    free(self);
    return NULL;
  }
  gm2_emu *emu = self->emu;
  gm2_reset_regs(emu);
  int status = -1;
  if (pitch_call(emu, PITCH_BLOCK_INIT, 0u) == 0 && pitch_call(emu, PITCH_BLOCK_RING, 0u) == 0 &&
      gm2_pitch_subtype(emu, block->subtype) == 0) {
    const uint32_t table = gm2_read(emu, GM2_X, PITCH_TABLE_POINTER + block->subtype);
    if (table == 0u) {
      fprintf(stderr, "%s: subtype %u has no descriptor table\n", effect->name, block->subtype);
    } else if (pitch_descriptor(self, table) == 0 && pitch_raw(self, table) == 0 &&
               pitch_levels_check(self) == 0 && pitch_rest(self) == 0 &&
               pitch_check_jobs(self) == 0) {
      status = 0;
    }
  } else {
    fprintf(stderr, "%s: the Pitch initializer did not return\n", effect->name);
  }
  if (status != 0) {
    pitch_block_close(self);
    return NULL;
  }
  return self;
}

/* The records the firmware holds for the block, in list order, against k_jobs:
 * every record it holds is one the adapter runs, with the payload it runs it on. */
static int pitch_check_jobs(const gm2_firmware *self) {
  uint32_t list[PITCH_JOBS + 2u];
  const unsigned count = gm2_firmware_jobs(self->emu, list, PITCH_JOBS + 2u);
  if (count == 0u) {
    fprintf(stderr, "%s: cannot read the block's job records\n", self->effect->name);
    return -1;
  }
  if (count != PITCH_JOBS) {
    fprintf(stderr, "%s: the block holds %u job records, not %u\n", self->effect->name, count,
            PITCH_JOBS);
    return -1;
  }
  for (unsigned i = 0; i < PITCH_JOBS; i++) {
    const pitch_job *job = &k_jobs[i];
    const uint32_t params = gm2_read(self->emu, GM2_X, list[i] + GM2_JOB_PARAMS);
    const uint32_t handler = gm2_read(self->emu, GM2_X, list[i] + GM2_JOB_HANDLER);
    if (list[i] != job->record || params != job->params || handler != job->handler) {
      fprintf(stderr,
              "%s: job %u is X:0x%06X {params X:0x%06X, handler P:0x%06X}, not X:0x%06X "
              "{X:0x%06X, P:0x%06X}\n",
              self->effect->name, i, list[i], params, handler, job->record, job->params,
              job->handler);
      return -1;
    }
    for (unsigned word = 0; word < 5u; word++) {
      const int32_t value = gm2_sign24(gm2_read(self->emu, GM2_X, job->params + word));
      if (value != job->word[word]) {
        fprintf(stderr, "%s: job %u's word %u is %d, not %d\n", self->effect->name, i, word,
                (int)value, (int)job->word[word]);
        return -1;
      }
    }
  }
  return 0;
}

/* The block's four jobs once, each as the runner P:0x0001CD runs it: R0 on its
 * parameters record, then its handler. */
static int pitch_run_jobs(gm2_firmware *self) {
  uint32_t record[PITCH_JOBS];
  for (unsigned i = 0; i < PITCH_JOBS; i++) {
    record[i] = k_jobs[i].record;
  }
  return gm2_firmware_run_list(self->emu, record, PITCH_JOBS);
}

/* The knob vector is the host's, over whatever a callback wrote back. */
static void pitch_restore_raw(gm2_firmware *self, const uint32_t *before) {
  for (unsigned i = 0; i < PITCH_RAW_WORDS; i++) {
    if (gm2_read(self->emu, GM2_X, PITCH_PARAMS + i) != before[i]) {
      gm2_write(self->emu, GM2_X, PITCH_PARAMS + i, before[i]);
    }
  }
}

/* The device's own path for a knob turn, the parameter edit P:0x083268: it clamps
 * the value to the knob's window, writes the raw word, runs the entry's callback
 * and, when the entry's format word asks for it, the callback again with A = 0x50. */
static int pitch_set_knob(gm2_firmware *self, unsigned knob, int value) {
  uint32_t before[PITCH_RAW_WORDS];
  for (unsigned i = 0; i < PITCH_RAW_WORDS; i++) {
    before[i] = gm2_read(self->emu, GM2_X, PITCH_PARAMS + i);
  }
  before[self->block->entry[knob]] = gm2_word24(value);
  if (gm2_firmware_edit(self->emu, (PITCH_ALGORITHM << 4) | self->block->entry[knob], PITCH_EDIT,
                        value, GM2_DEVICE_STACK) != 0) {
    fprintf(stderr, "%s: the parameter edit P:0x%06X did not return for knob %u\n",
            self->effect->name, PITCH_EDIT, knob);
    return -1;
  }
  pitch_restore_raw(self, before);
  /* The knob values alone decide the sound: a Scale turn rebuilds the harmony grid
   * but not the curves derived from it, and the feedback clamp writes its own raw
   * word back. */
  if (pitch_call(self->emu, PITCH_RATIOS, PITCH_UI) != 0 ||
      pitch_call(self->emu, PITCH_LEVELS, PITCH_UI) != 0) {
    fprintf(stderr, "%s: the parameter routines did not return\n", self->effect->name);
    return -1;
  }
  pitch_restore_raw(self, before);
  /* The edit clobbers the registers the continuation runs with. */
  self->audio_dirty = 1;
  return pitch_check_jobs(self);
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int pitch_level_words(gm2_firmware *self, int32_t level[3]) {
  for (unsigned i = 0; i < PITCH_LEVEL_WORDS; i++) {
    level[i] = gm2_sign24(gm2_read(self->emu, GM2_X, PITCH_LEVEL_STAGED + i));
  }
  return 0;
}

static void pitch_audio_begin(gm2_emu *emu) {
  gm2_reset_regs(emu);
  for (unsigned reg = DSP56300_REG_M0; reg <= DSP56300_REG_M7; reg++) {
    gm2_set_reg(emu, reg, 0xFFFFFFu);
  }
  gm2_set_reg(emu, DSP56300_REG_SR, 0x800300u);
}

/* One frame at the continuation's own entry, R6 on the task record: the chunk
 * word on a chunk's first frame and the scratch continuation on its last. */
static int pitch_frame(gm2_firmware *self, int32_t left, int32_t right, int32_t *out_left,
                       int32_t *out_right) {
  const unsigned chunk = self->frame % PITCH_CHUNK_FRAMES;
  if (self->audio_dirty != 0) {
    pitch_audio_begin(self->emu);
    self->audio_dirty = 0;
  }
  gm2_write(self->emu, GM2_X, PITCH_INPUT, gm2_word24(left));
  gm2_write(self->emu, GM2_X, PITCH_INPUT + 1u, gm2_word24(right));
  gm2_write(self->emu, GM2_X, PITCH_CHUNK_WORD, chunk == 0u ? 4u : 0u);
  gm2_write(self->emu, GM2_Y, PITCH_LINK,
            chunk == PITCH_CHUNK_FRAMES - 1u ? GM2_SCRATCH_ADDR : GM2_STUB_ADDR + 2u);
  gm2_set_reg(self->emu, DSP56300_REG_R6, PITCH_TASK_RECORD);
  gm2_set_reg(self->emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  gm2_set_reg(self->emu, DSP56300_REG_M6, 0xFFFFFFu);
  /* The L flag is sticky, so a previous frame's railing store would otherwise
   * reach this frame's voice splice test. */
  gm2_set_reg(self->emu, DSP56300_REG_SR, 0x800300u);
  if (gm2_run_until_stop(self->emu, PITCH_ENTRY, PITCH_FRAME_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "%s: audio frame %u never reached the stop\n", self->effect->name, self->frame);
    return -1;
  }
  *out_left = gm2_sign24(gm2_read(self->emu, GM2_X, PITCH_OUTPUT));
  *out_right = gm2_sign24(gm2_read(self->emu, GM2_X, PITCH_OUTPUT + 1u));
  self->frame++;
  if (chunk != PITCH_CHUNK_FRAMES - 1u) {
    return 0;
  }
  return pitch_run_jobs(self);
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int pitch_words(gm2_firmware *self, int32_t *words) {
  unsigned at = 0;
  for (unsigned i = 0; i < GM2_PITCH_CORE_VOICES; i++) {
    words[at++] = gm2_sign24(gm2_read(self->emu, GM2_X, PITCH_STAGED + i));
  }
  for (unsigned i = 0; i < GM2_PITCH_CORE_VOICES; i++) {
    words[at++] = gm2_sign24(gm2_read(self->emu, GM2_X, 0x0412u + 2u * i));
  }
  for (unsigned i = 0; i < GM2_PITCH_CORE_VOICES; i++) {
    words[at++] = gm2_sign24(gm2_read(self->emu, GM2_X, 0x041Du + i));
  }
  for (unsigned i = 0; i < 4u; i++) {
    words[at++] = gm2_sign24(gm2_read(self->emu, GM2_X, PITCH_STAGED + 2u + i));
  }
  for (unsigned voice = 0; voice < GM2_PITCH_CORE_VOICES; voice++) {
    words[at++] = gm2_sign24(gm2_read(self->emu, GM2_Y, 0x0228u + 2u * voice));
  }
  for (unsigned voice = 0; voice < GM2_PITCH_CORE_VOICES; voice++) {
    for (unsigned i = 0; i < GM2_PITCH_CORE_CURVE_WORDS; i++) {
      words[at++] =
          gm2_sign24(gm2_read(self->emu, GM2_Y, 0x022Bu + GM2_PITCH_CORE_CURVE_WORDS * voice + i));
    }
  }
  words[at++] = gm2_sign24(gm2_read(self->emu, GM2_X, 0x0416u));
  words[at++] = gm2_sign24(gm2_read(self->emu, GM2_X, 0x0423u));
  words[at++] = gm2_sign24(gm2_read(self->emu, GM2_X, 0x0424u));
  for (unsigned i = 0; i < 3u; i++) {
    words[at++] = gm2_sign24(gm2_read(self->emu, GM2_X, 0x0566u + i));
  }
  words[at++] = gm2_sign24(gm2_read(self->emu, GM2_X, PITCH_LEVEL_STAGED + 1u));
  words[at++] = gm2_sign24(gm2_read(self->emu, GM2_X, PITCH_LEVEL_STAGED + 2u));
  return 0;
}

#define PITCH_ADAPTER(name, subtype, entries)                                                      \
  static gm2_firmware *name##_open(void) {                                                         \
    static const pitch_block block = {subtype, (unsigned)(sizeof entries / sizeof entries[0]),     \
                                      entries};                                                    \
    return pitch_block_open(&gm2_effect_##name, &block);                                           \
  }                                                                                                \
  const gm2_firmware_ops gm2_firmware_##name = {                                                   \
      .effect = &gm2_effect_##name,                                                                \
      .open = name##_open,                                                                         \
      .levels = pitch_level_words,                                                                 \
      .set_knob = pitch_set_knob,                                                                  \
      .words = pitch_words,                                                                        \
      .frame = pitch_frame,                                                                        \
      .close = pitch_block_close,                                                                  \
  }

PITCH_ADAPTER(detune, 0u, k_detune_entry);
PITCH_ADAPTER(whammy, 1u, k_whammy_entry);
PITCH_ADAPTER(octaver, 2u, k_octaver_entry);
PITCH_ADAPTER(pitch, 3u, k_pitch_entry);
PITCH_ADAPTER(ipitch, 4u, k_pitch_entry);
