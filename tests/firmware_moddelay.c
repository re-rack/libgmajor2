/* The Modulation block subtypes 0..8 as the firmware runs them: the parameter
 * setup P:0x0938BE and its descriptor callbacks, the Out Level updater
 * P:0x093FA1, the twelve job records the activation leaves (five smoothing
 * records, six tap crossfades, and the level record the level code queues) with
 * the settled check P:0x00033C the manager P:0x0960B4 runs for them, and the
 * audio continuation P:0x001117 with its task record Y:0x049B.
 */
#include "firmware_common.h"
#include "gm2_effect.h"
#include "gm2_emu.h"
#include "gm2_record.h"

#include <dsp56300.h>

#include <stdio.h>
#include <stdlib.h>

#define MOD_ALGORITHM 3u
#define MOD_PARAMS 0x099B66u /* X: the accessor's sixteen raw words */
#define MOD_PARAM_WORDS 16u
#define MOD_PARAM_SLOT_SUBTYPE 0u
#define MOD_PARAM_SLOT_MIX 13u /* 100 % */
#define MOD_PARAM_SLOT_OUT_LEVEL 14u /* 0 dB */
#define MOD_PARAM_MIX 100
#define MOD_PARAM_OUT_LEVEL 0
#define MOD_TAP_TIME 0x099A66u
#define MOD_BEAT 0x099A6Bu
#define MOD_UI 0x098C7Fu
#define MOD_ACTIVATION 0x0938BEu
#define MOD_OUT_LEVEL_UPDATER 0x093FA1u
/* The value the second callback call carries, which P:0x0832A0 passes after
 * storing the raw word at X:0x09860C. */
#define MOD_SECOND_CALL 0x50u
#define MOD_DESCRIPTOR_FLAGS 12u
#define MOD_DESCRIPTOR_DEFAULT 13u
#define MOD_DESCRIPTOR_STRIDE 15u
#define MOD_CURRENT_VALUE 0x09860Cu /* X: the raw value the parameter path is editing */

/* UI+1 gates the activation's job queueing, UI+3 is the publish flag the block's
 * dispatcher raises around a setup-chain run, and UI+4 is the bypass flag the
 * updaters return on. */
#define MOD_RUN_FLAG 0x098C80u
#define MOD_PUBLISH_FLAG 0x098C82u
#define MOD_BYPASS_FLAG 0x098C83u

#define MOD_LEVEL_RECORD 0x098D13u
#define MOD_LEVEL_STAGED 0x098D19u
#define MOD_LEVEL_LIVE 0x04F6u
#define MOD_LEVEL_WORDS 3u

#define MOD_STAGED 0x098BD1u /* X: S+0..29 */
#define MOD_STAGE_WORDS 30u
#define MOD_DEPTH 0x04BAu /* Y: five words per voice */
#define MOD_TAP_RECORD 0x098C01u /* X: eleven words per voice */
#define MOD_TAP_RECORD_WORDS 11u
#define MOD_TAP_RECORD_GAIN 2u   /* the record's two gain addresses */
#define MOD_TAP_RECORD_RAIL 4u   /* the gains the crossfade clamps onto at a rail */
#define MOD_TAP_RECORD_COPY 6u   /* the gain the check hands the silent tap */
#define MOD_TAP_RECORD_OFFSET 8u /* the record's two Y offset addresses */
#define MOD_TAP_RECORD_STEP 10u  /* the record's crossfade step */
#define MOD_TAP_STAGED_WORD 7u   /* the record's staged offset */
#define MOD_TAP_GAIN 0x04D8u     /* Y: two words per voice, in voice order */
#define MOD_TAP_OFFSET 0x04BCu   /* Y: a pair every five words, in voice order */
#define MOD_TAP_TARGET 0x7FFFFFu
#define MOD_TAP_STEP 0x008312u
#define MOD_VOICES 6u

#define MOD_SLOT_SPEED 1u
#define MOD_SLOT_DEPTH 3u
#define MOD_SLOT_GOLD 9u

/* The handlers the block's job records name. */
#define MOD_JOB_TAP 0x00033Au
/* P:0x093C85: the rate and depth updater the setup chain and the Speed and Tempo
 * callbacks run; with a nonzero argument it takes the tempo's rate whenever Tempo
 * is set. */
#define MOD_RATE_UPDATER 0x093C85u
#define MOD_RATE_TEMPO_ARG 0xFFFFFFu

#define MOD_RECORDS 12u

#define MOD_KERNEL 0x001117u
#define MOD_TASK 0x049Bu
#define MOD_TASK_ENTRY_OFFSET 1u
#define MOD_TASK_INPUT_OFFSET 15u
#define MOD_AUDIO_IN 0x0050u
#define MOD_AUDIO_OUT 0x0052u
#define MOD_LINK_RECORD 0x041Eu
#define MOD_BUFFER 0x080000u
#define MOD_BUFFER_WORDS 0x2000u

#define MOD_BUDGET 2000000
#define MOD_SETTLE_ROUNDS 8000u

#define MODDELAY_KNOBS_MAX 9u

typedef struct {
  const gm2_effect *effect;
  unsigned subtype;
  const unsigned char *slot; /* the firmware raw word each knob holds */
} moddelay_block;

static const unsigned char k_cchorus_slot[] = {1, 3, 4, 5};
static const unsigned char k_achorus_slot[] = {1, 3, 4, 5, 8, 9, 12};
static const unsigned char k_cflanger_slot[] = {1, 3, 4, 5, 6, 7};
static const unsigned char k_aflanger_slot[] = {1, 3, 4, 5, 6, 7, 8, 9, 12};
static const unsigned char k_trichorus_slot[] = {1, 3, 4, 8, 12};
static const unsigned char k_tzflanger_slot[] = {1, 3, 4, 6, 7};
static const unsigned char k_vibrato_slot[] = {1, 3, 4, 5};

/* The nine effects in fixture order. */
static const moddelay_block k_blocks[] = {
    {&gm2_effect_cchorus, 0u, k_cchorus_slot},
    {&gm2_effect_achorus, 1u, k_achorus_slot},
    {&gm2_effect_cflanger, 2u, k_cflanger_slot},
    {&gm2_effect_aflanger, 3u, k_aflanger_slot},
    {&gm2_effect_trichorus, 4u, k_trichorus_slot},
    {&gm2_effect_trichorus_asym, 5u, k_trichorus_slot},
    {&gm2_effect_tzflanger_neg, 6u, k_tzflanger_slot},
    {&gm2_effect_tzflanger_pos, 7u, k_tzflanger_slot},
    {&gm2_effect_vibrato, 8u, k_vibrato_slot},
};

typedef struct {
  uint32_t params;
  uint32_t handler;
} moddelay_record;

/* The records the block's activation leaves. The five smoothing payloads carry
 * {X source, Y destination, word count} and cover S+0..29 exactly once; the level
 * record covers X:0x098D19..X:0x098D1B. */
static const moddelay_record k_records[MOD_RECORDS] = {
    {MOD_STAGED + 0x1Eu, GM2_JOB_SMOOTH}, /* S+0..11 */
    {MOD_STAGED + 0x24u, GM2_JOB_SMOOTH}, /* S+12 */
    {MOD_STAGED + 0x2Au, GM2_JOB_SMOOTH}, /* S+13 */
    {MOD_STAGED + 0x72u, GM2_JOB_GROUP},  /* S+14..23 */
    {MOD_STAGED + 0x7Au, GM2_JOB_GROUP},  /* S+24..29 */
    {MOD_TAP_RECORD + 0u * MOD_TAP_RECORD_WORDS, MOD_JOB_TAP},
    {MOD_TAP_RECORD + 1u * MOD_TAP_RECORD_WORDS, MOD_JOB_TAP},
    {MOD_TAP_RECORD + 2u * MOD_TAP_RECORD_WORDS, MOD_JOB_TAP},
    {MOD_TAP_RECORD + 3u * MOD_TAP_RECORD_WORDS, MOD_JOB_TAP},
    {MOD_TAP_RECORD + 4u * MOD_TAP_RECORD_WORDS, MOD_JOB_TAP},
    {MOD_TAP_RECORD + 5u * MOD_TAP_RECORD_WORDS, MOD_JOB_TAP},
    {MOD_LEVEL_RECORD, GM2_JOB_SMOOTH},
};

struct gm2_firmware {
  gm2_emu *emu;
  const moddelay_block *block;
  uint32_t table;
  uint32_t callback[MODDELAY_KNOBS_MAX];
  int32_t value[MODDELAY_KNOBS_MAX];
  uint32_t record[MOD_RECORDS];   /* the block's job records, in list order */
  unsigned records;
  uint32_t live[MOD_STAGE_WORDS]; /* Y: where each staged word's copy sits */
  uint32_t level_live[MOD_LEVEL_WORDS];
  unsigned chunk_pos;
};

static int moddelay_call(gm2_firmware *self, uint32_t entry, uint32_t record) {
  gm2_set_reg(self->emu, DSP56300_REG_R0, record);
  gm2_set_reg(self->emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  if (gm2_call(self->emu, entry, MOD_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "%s: P:0x%06X did not return\n", self->block->effect->name, entry);
    return -1;
  }
  return 0;
}

/* A voice's crossfade still has work while its record's step is armed or its two
 * taps have not come to hold the same offset. */
static unsigned moddelay_fades_moving(const gm2_firmware *self) {
  unsigned moving = 0;
  for (unsigned voice = 0; voice < MOD_VOICES; voice++) {
    const uint32_t record = MOD_TAP_RECORD + MOD_TAP_RECORD_WORDS * voice;
    const uint32_t tap0 = gm2_read(self->emu, GM2_X, record + MOD_TAP_RECORD_OFFSET);
    const uint32_t tap1 = gm2_read(self->emu, GM2_X, record + MOD_TAP_RECORD_OFFSET + 1u);
    moving += gm2_read(self->emu, GM2_X, record) != 0u;
    moving += gm2_read(self->emu, GM2_Y, tap0) != gm2_read(self->emu, GM2_Y, tap1);
  }
  return moving;
}

/* The tap records' payloads, which are the words the audio kernel blends with and
 * the crossfade P:0x00033A steps. */
static int moddelay_check_taps(const gm2_firmware *self) {
  const gm2_effect *effect = self->block->effect;
  for (unsigned voice = 0; voice < MOD_VOICES; voice++) {
    const uint32_t record = MOD_TAP_RECORD + MOD_TAP_RECORD_WORDS * voice;
    const uint32_t gain = MOD_TAP_GAIN + 2u * voice;
    const uint32_t offset = MOD_TAP_OFFSET + 5u * voice;
    const struct {
      unsigned word;
      uint32_t value;
    } want[] = {
        {MOD_TAP_RECORD_GAIN, gain},
        {MOD_TAP_RECORD_GAIN + 1u, gain + 1u},
        {MOD_TAP_RECORD_RAIL, MOD_TAP_TARGET},
        {MOD_TAP_RECORD_RAIL + 1u, MOD_TAP_TARGET},
        {MOD_TAP_RECORD_COPY, MOD_TAP_TARGET},
        {MOD_TAP_RECORD_OFFSET, offset},
        {MOD_TAP_RECORD_OFFSET + 1u, offset + 2u},
        {MOD_TAP_RECORD_STEP, MOD_TAP_STEP},
    };
    for (unsigned i = 0; i < sizeof want / sizeof want[0]; i++) {
      const uint32_t got = gm2_read(self->emu, GM2_X, record + want[i].word);
      if (got != want[i].value) {
        fprintf(stderr, "%s: tap record X:0x%06X word %u is %06X, not %06X\n", effect->name, record,
                want[i].word, got, want[i].value);
        return -1;
      }
    }
  }
  return 0;
}

/* The block's job list, read fresh and checked against the twelve records its
 * activation leaves: a record the adapter does not know, or one naming another
 * payload, changes what the block does after a chunk. */
static int moddelay_jobs(gm2_firmware *self) {
  uint32_t record[MOD_RECORDS + 1u];
  const gm2_effect *effect = self->block->effect;
  const unsigned count = gm2_firmware_jobs(self->emu, record, MOD_RECORDS + 1u);
  if (count != MOD_RECORDS) {
    fprintf(stderr, "%s: the block's job list is not the %u records its activation leaves\n",
            effect->name, MOD_RECORDS);
    return -1;
  }
  unsigned seen[MOD_RECORDS] = {0};
  for (unsigned i = 0; i < count; i++) {
    const uint32_t handler = gm2_read(self->emu, GM2_X, record[i] + GM2_JOB_HANDLER);
    const uint32_t params = gm2_read(self->emu, GM2_X, record[i] + GM2_JOB_PARAMS);
    unsigned match = 0;
    while (match < MOD_RECORDS &&
           (k_records[match].params != params || k_records[match].handler != handler)) {
      match++;
    }
    if (match == MOD_RECORDS || seen[match] != 0u) {
      fprintf(stderr, "%s: job X:0x%06X runs P:0x%06X on X:0x%06X, not a record of the block\n",
              effect->name, record[i], handler, params);
      return -1;
    }
    seen[match] = 1u;
    self->record[i] = record[i];
  }
  self->records = count;
  return moddelay_check_taps(self);
}

/* Where each staged word's live copy sits, from the payloads the records name:
 * {X source, Y destination, word count, snap threshold, step}. */
static int moddelay_map_live(gm2_firmware *self) {
  const gm2_effect *effect = self->block->effect;
  unsigned mapped[MOD_STAGE_WORDS] = {0};
  unsigned levels = 0;
  for (unsigned i = 0; i < self->records; i++) {
    if (gm2_read(self->emu, GM2_X, self->record[i] + GM2_JOB_HANDLER) == MOD_JOB_TAP) {
      continue;
    }
    const uint32_t params = gm2_read(self->emu, GM2_X, self->record[i] + GM2_JOB_PARAMS);
    const uint32_t source = gm2_read(self->emu, GM2_X, params);
    const uint32_t target = gm2_read(self->emu, GM2_X, params + GM2_JOB_DST);
    const uint32_t words = gm2_read(self->emu, GM2_X, params + GM2_JOB_COUNT);
    for (uint32_t w = 0; w < words; w++) {
      if (source == MOD_LEVEL_STAGED) {
        if (levels < MOD_LEVEL_WORDS) {
          self->level_live[levels++] = target + w;
        }
        continue;
      }
      const uint32_t index = source + w - MOD_STAGED;
      if (source < MOD_STAGED || index >= MOD_STAGE_WORDS || mapped[index] != 0u) {
        fprintf(stderr,
                "%s: job X:0x%06X moves X:0x%06X onto Y:0x%06X, outside the block's words\n",
                effect->name, self->record[i], source + w, target + w);
        return -1;
      }
      mapped[index] = 1u;
      self->live[index] = target + w;
    }
  }
  for (unsigned i = 0; i < MOD_STAGE_WORDS; i++) {
    if (mapped[i] == 0u) {
      fprintf(stderr, "%s: no job moves staged word S+%u\n", effect->name, i);
      return -1;
    }
  }
  if (levels != MOD_LEVEL_WORDS || self->level_live[0] != MOD_LEVEL_LIVE) {
    fprintf(stderr, "%s: the records move %u level words onto Y:0x%06X\n", effect->name, levels,
            self->level_live[0]);
    return -1;
  }
  return 0;
}

/* One pass over the block's records in list order, each as the manager
 * P:0x0960C6 and the runner P:0x0001CD hand it over: the record's settled check,
 * then its handler. */
static int moddelay_run_list(gm2_firmware *self) {
  return gm2_firmware_run_list(self->emu, self->record, self->records);
}

/* The rest state: every staged word on its live copy, the level words glided up
 * from their power-on zero, and every voice's crossfade run out. */
static int moddelay_settle(gm2_firmware *self) {
  for (unsigned round = 0; round < MOD_SETTLE_ROUNDS; round++) {
    if (moddelay_run_list(self) != 0) {
      return -1;
    }
    unsigned settled = 0;
    for (unsigned i = 0; i < MOD_STAGE_WORDS; i++) {
      settled +=
          gm2_read(self->emu, GM2_Y, self->live[i]) == gm2_read(self->emu, GM2_X, MOD_STAGED + i);
    }
    for (unsigned i = 0; i < MOD_LEVEL_WORDS; i++) {
      settled += gm2_read(self->emu, GM2_Y, self->level_live[i]) ==
                 gm2_read(self->emu, GM2_X, MOD_LEVEL_STAGED + i);
    }
    if (settled == MOD_STAGE_WORDS + MOD_LEVEL_WORDS && moddelay_fades_moving(self) == 0u) {
      return 0;
    }
  }
  fprintf(stderr, "%s: the staged words never settled\n", self->block->effect->name);
  return -1;
}

/* The descriptor table the selected subtype runs, from the sub-record the
 * accessor reads. */
static int moddelay_descriptor(gm2_firmware *self) {
  const uint32_t record = gm2_read(self->emu, GM2_X, GM2_CHAIN + MOD_ALGORITHM);
  const uint32_t table = gm2_read(self->emu, GM2_P, record + 7u);
  if (record == 0u || table == 0u) {
    fprintf(stderr, "%s: the Modulation sub-record has no descriptor table\n",
            self->block->effect->name);
    return -1;
  }
  self->table = table;
  for (unsigned knob = 0; knob < self->block->effect->knobs; knob++) {
    const uint32_t entry = table + MOD_DESCRIPTOR_STRIDE * self->block->slot[knob];
    if (gm2_firmware_knob_entry(self->emu, table, self->block->slot[knob],
                                &self->block->effect->knob[knob], &self->callback[knob]) != 0) {
      return -1;
    }
    const int32_t def = gm2_sign24(gm2_read(self->emu, GM2_P, entry + MOD_DESCRIPTOR_DEFAULT));
    if (def != self->block->effect->knob[knob].def) {
      fprintf(stderr, "%s: %s defaults to %d in the firmware, %d in the knob table\n",
              self->block->effect->name, self->block->effect->knob[knob].name, def,
              self->block->effect->knob[knob].def);
      return -1;
    }
  }
  return 0;
}

/* A callback that rewrites another knob's raw word (the tempo path writes the
 * nearest Speed) leaves the host's value behind, without that knob's callback. */
static void moddelay_restore(gm2_firmware *self) {
  const gm2_effect *effect = self->block->effect;
  for (unsigned other = 0; other < effect->knobs; other++) {
    const uint32_t at = MOD_PARAMS + self->block->slot[other];
    if (gm2_sign24(gm2_read(self->emu, GM2_X, at)) != self->value[other]) {
      gm2_write(self->emu, GM2_X, at, (uint32_t)self->value[other] & 0xFFFFFFu);
    }
  }
}

/* The Speed callback runs P:0x093C85 and the Depth and Gold ones P:0x093B64,
 * each with a clear flag, so the rate and the Golden Ratio's depth take the
 * speed's rate even under a Tempo value; a Tempo turn's own A = 0x50 path derives
 * both from the tempo. With a nonzero argument P:0x093C85 is the setup chain's
 * way back to the words the knob values alone decide. */
static int moddelay_rebuild(gm2_firmware *self) {
  gm2_set_reg(self->emu, DSP56300_REG_A2, 0);
  gm2_set_reg(self->emu, DSP56300_REG_A1, MOD_RATE_TEMPO_ARG);
  gm2_set_reg(self->emu, DSP56300_REG_A0, 0);
  if (moddelay_call(self, MOD_RATE_UPDATER, MOD_UI) != 0) {
    return -1;
  }
  moddelay_restore(self);
  return 0;
}

/* One knob turn the device's way: the raw word, then P:0x08328D's call of the
 * descriptor callback with A holding the value, and on a parameter whose
 * descriptor flags ask for the second call the same callback again with A = 0x50.
 * The publish flag stays clear, so the updaters only restage their words and the
 * block's jobs glide them onto the live ones. */
static int moddelay_set_knob(gm2_firmware *self, unsigned knob, int value) {
  const gm2_effect *effect = self->block->effect;
  if (knob >= effect->knobs) {
    return -1;
  }
  const unsigned slot = self->block->slot[knob];
  const uint32_t raw = (uint32_t)value & 0xFFFFFFu;
  const uint32_t entry = self->table + MOD_DESCRIPTOR_STRIDE * slot;
  gm2_write(self->emu, GM2_X, MOD_PARAMS + slot, raw);
  const uint32_t flags = gm2_read(self->emu, GM2_P, entry + MOD_DESCRIPTOR_FLAGS);
  const uint32_t calls = gm2_firmware_second_call(flags) ? 2u : 1u;
  for (unsigned call = 0; call < calls; call++) {
    if (call != 0u) {
      /* The second call reads the value from the accessor's current-word slot. */
      gm2_write(self->emu, GM2_X, MOD_CURRENT_VALUE, raw);
    }
    gm2_set_reg(self->emu, DSP56300_REG_R0, MOD_PARAMS);
    gm2_set_reg(self->emu, DSP56300_REG_R4, entry);
    gm2_set_reg(self->emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
    gm2_set_reg(self->emu, DSP56300_REG_A2, 0);
    gm2_set_reg(self->emu, DSP56300_REG_A1,
                call == 0u ? (uint32_t)value & 0xFFFFFFu : MOD_SECOND_CALL);
    gm2_set_reg(self->emu, DSP56300_REG_A0, 0);
    if (gm2_call(self->emu, self->callback[knob], MOD_BUDGET, NULL) != GM2_CALL_OK) {
      fprintf(stderr, "%s: the callback P:0x%06X did not return for %s\n", effect->name,
              self->callback[knob], effect->knob[knob].name);
      return -1;
    }
  }
  self->value[knob] = (int32_t)value;
  moddelay_restore(self);
  if (slot == MOD_SLOT_SPEED || slot == MOD_SLOT_DEPTH || slot == MOD_SLOT_GOLD) {
    if (moddelay_rebuild(self) != 0) {
      return -1;
    }
  }
  return moddelay_jobs(self);
}

static void moddelay_task_record(gm2_emu *emu) {
  gm2_write(emu, GM2_Y, MOD_TASK, MOD_AUDIO_IN);
  gm2_write(emu, GM2_Y, MOD_TASK + MOD_TASK_INPUT_OFFSET, MOD_AUDIO_IN);
  gm2_write(emu, GM2_Y, MOD_TASK + MOD_TASK_INPUT_OFFSET + 1u, MOD_AUDIO_OUT);
  gm2_write(emu, GM2_Y, MOD_TASK + MOD_TASK_INPUT_OFFSET + 2u, MOD_LINK_RECORD);
  gm2_write(emu, GM2_Y, MOD_LINK_RECORD, GM2_STUB_ADDR + 2u);
}

static gm2_firmware *moddelay_open(const moddelay_block *block) {
  gm2_firmware *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->block = block;
  self->emu = gm2_firmware_boot(block->effect->name);
  if (self->emu == NULL || gm2_mod_subtype(self->emu, block->subtype) != 0 ||
      moddelay_descriptor(self) != 0) {
    goto fail;
  }
  /* The device's factory bank: every raw slot at its descriptor default, then the
   * subtype, the knobs and the fixed Mix and Out Level over it. */
  for (unsigned slot = 0; slot < MOD_PARAM_WORDS; slot++) {
    gm2_write(self->emu, GM2_X, MOD_PARAMS + slot,
              gm2_read(self->emu, GM2_P,
                       self->table + MOD_DESCRIPTOR_STRIDE * slot + MOD_DESCRIPTOR_DEFAULT));
  }
  gm2_write(self->emu, GM2_X, MOD_PARAMS + MOD_PARAM_SLOT_SUBTYPE, block->subtype);
  for (unsigned knob = 0; knob < block->effect->knobs; knob++) {
    const int32_t value = block->effect->knob[knob].def;
    gm2_write(self->emu, GM2_X, MOD_PARAMS + block->slot[knob], (uint32_t)value & 0xFFFFFFu);
    self->value[knob] = value;
  }
  gm2_write(self->emu, GM2_X, MOD_PARAMS + MOD_PARAM_SLOT_MIX, MOD_PARAM_MIX);
  gm2_write(self->emu, GM2_X, MOD_PARAMS + MOD_PARAM_SLOT_OUT_LEVEL, MOD_PARAM_OUT_LEVEL);
  gm2_write(self->emu, GM2_X, MOD_TAP_TIME, 1u);
  gm2_write(self->emu, GM2_X, MOD_BEAT, GM2_EFFECT_BEAT_MS);
  gm2_write(self->emu, GM2_X, MOD_RUN_FLAG, 1u);
  gm2_write(self->emu, GM2_X, MOD_BYPASS_FLAG, 0u);
  /* The block's dispatcher raises the publish flag around the setup chain it runs
   * when the device selects the effect, so that chain's restaged words go live at
   * once and the depth latch takes the new depth. A knob turn runs with the flag
   * clear and the block's jobs glide the words instead. */
  gm2_write(self->emu, GM2_X, MOD_PUBLISH_FLAG, 1u);
  gm2_set_reg(self->emu, DSP56300_REG_A2, 0);
  gm2_set_reg(self->emu, DSP56300_REG_A1, MOD_ALGORITHM);
  gm2_set_reg(self->emu, DSP56300_REG_A0, 0);
  if (moddelay_call(self, MOD_ACTIVATION, MOD_UI) != 0 ||
      moddelay_call(self, MOD_OUT_LEVEL_UPDATER, MOD_UI) != 0) {
    goto fail;
  }
  gm2_write(self->emu, GM2_X, MOD_PUBLISH_FLAG, 0u);
  if (moddelay_jobs(self) != 0 || moddelay_map_live(self) != 0 || moddelay_settle(self) != 0) {
    goto fail;
  }
  /* X:0x080000..X:0x081FFF hold the load image's records until the two delay
   * lines take the addresses; the device starts them empty. */
  for (uint32_t i = 0; i < MOD_BUFFER_WORDS; i++) {
    gm2_raw(self->emu, GM2_X)[MOD_BUFFER + i] = 0;
  }
  if (gm2_read(self->emu, GM2_Y, MOD_TASK - MOD_TASK_ENTRY_OFFSET) != MOD_KERNEL) {
    fprintf(stderr, "%s: the task record names P:0x%06X, not P:0x%06X\n", block->effect->name,
            gm2_read(self->emu, GM2_Y, MOD_TASK - MOD_TASK_ENTRY_OFFSET), MOD_KERNEL);
    goto fail;
  }
  moddelay_task_record(self->emu);
  return self;

fail:
  if (self->emu != NULL) {
    gm2_destroy(self->emu);
  }
  free(self);
  return NULL;
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int moddelay_levels(gm2_firmware *self, int32_t level[3]) {
  for (unsigned i = 0; i < MOD_LEVEL_WORDS; i++) {
    const uint32_t live = gm2_read(self->emu, GM2_Y, self->level_live[i]);
    const uint32_t staged = gm2_read(self->emu, GM2_X, MOD_LEVEL_STAGED + i);
    if (live != staged) {
      fprintf(stderr, "%s: level word Y:0x%06X is %06X, X:0x%06X stages %06X\n",
              self->block->effect->name, self->level_live[i], live, MOD_LEVEL_STAGED + i, staged);
      return -1;
    }
    level[i] = gm2_sign24(staged);
  }
  return 0;
}

// cppcheck-suppress constParameterCallback -- the ops signature fixes the pointer
static int moddelay_words(gm2_firmware *self, int32_t *words) {
  for (unsigned i = 0; i < MOD_STAGE_WORDS; i++) {
    words[i] = gm2_sign24(gm2_read(self->emu, GM2_X, MOD_STAGED + i));
  }
  for (unsigned voice = 0; voice < MOD_VOICES; voice++) {
    words[MOD_STAGE_WORDS + voice] = gm2_sign24(gm2_read(self->emu, GM2_Y, MOD_DEPTH + 5u * voice));
    words[MOD_STAGE_WORDS + MOD_VOICES + voice] = gm2_sign24(gm2_read(
        self->emu, GM2_X, MOD_TAP_RECORD + MOD_TAP_RECORD_WORDS * voice + MOD_TAP_STAGED_WORD));
  }
  return 0;
}

static int moddelay_frame(gm2_firmware *self, int32_t left, int32_t right, int32_t *out_left,
                          int32_t *out_right) {
  gm2_write(self->emu, GM2_X, MOD_AUDIO_IN, gm2_word24(left));
  gm2_write(self->emu, GM2_X, MOD_AUDIO_IN + 1u, gm2_word24(right));
  gm2_set_reg(self->emu, DSP56300_REG_R6, MOD_TASK);
  gm2_set_reg(self->emu, DSP56300_REG_M6, 0xFFFFFFu);
  if (gm2_run_until_stop(self->emu, MOD_KERNEL, MOD_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "%s: the continuation never reached its stop\n", self->block->effect->name);
    return -1;
  }
  *out_left = gm2_sign24(gm2_read(self->emu, GM2_X, MOD_AUDIO_OUT));
  *out_right = gm2_sign24(gm2_read(self->emu, GM2_X, MOD_AUDIO_OUT + 1u));
  if (++self->chunk_pos < 4u) {
    return 0;
  }
  self->chunk_pos = 0;
  return moddelay_run_list(self);
}

static void moddelay_close(gm2_firmware *self) {
  if (self != NULL) {
    gm2_destroy(self->emu);
    free(self);
  }
}

#define MODDELAY_OPS(name, index)                                                                  \
  static gm2_firmware *name##_open(void) { return moddelay_open(&k_blocks[index]); }               \
  const gm2_firmware_ops gm2_firmware_##name = {                                                   \
      .effect = &gm2_effect_##name,                                                                \
      .open = name##_open,                                                                         \
      .levels = moddelay_levels,                                                                   \
      .set_knob = moddelay_set_knob,                                                               \
      .words = moddelay_words,                                                                     \
      .frame = moddelay_frame,                                                                     \
      .close = moddelay_close,                                                                     \
  }

MODDELAY_OPS(cchorus, 0);
MODDELAY_OPS(achorus, 1);
MODDELAY_OPS(cflanger, 2);
MODDELAY_OPS(aflanger, 3);
MODDELAY_OPS(trichorus, 4);
MODDELAY_OPS(trichorus_asym, 5);
MODDELAY_OPS(tzflanger_neg, 6);
MODDELAY_OPS(tzflanger_pos, 7);
MODDELAY_OPS(vibrato, 8);
