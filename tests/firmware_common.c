#include "firmware_common.h"

#include <dsp56300.h>

#include <stdio.h>
#include <string.h>

#define GM2_DESCRIPTOR_STRIDE 15u
#define GM2_DESCRIPTOR_NAME 10u
#define GM2_DESCRIPTOR_MIN 10u
#define GM2_DESCRIPTOR_MAX 11u
#define GM2_DESCRIPTOR_CALLBACK 14u

#define GM2_JOB_QUEUE_HEAD 0x0993C3u  /* X: head of the list P:0x096194 queues into */
#define GM2_JOB_QUEUE_COUNT 0x0993C0u /* X: how many records it holds */
#define GM2_JOB_ACTIVE_HEAD 0x0200u   /* X: head of the list P:0x096175 queues into */
#define GM2_JOB_ACTIVE_COUNT 0x0993C1u
#define GM2_JOB_CHECK 0x0960B4u /* P: the settled check the manager runs */
#define GM2_SETTLE_PASSES 8192u

gm2_emu *gm2_firmware_boot(const char *who) {
  gm2_emu *emu = gm2_create();
  if (emu == NULL) {
    fprintf(stderr, "%s: cannot create the emulator\n", who);
    return NULL;
  }
  if (gm2_load_application(emu, GM2_FIRMWARE_DATA_DIR, 0) != 0) {
    fprintf(stderr, "%s: cannot load the application image\n", who);
    gm2_destroy(emu);
    return NULL;
  }
  if (gm2_apply_init_table(emu) < 0) {
    fprintf(stderr, "%s: cannot apply the initializer\n", who);
    gm2_destroy(emu);
    return NULL;
  }
  gm2_reset_regs(emu);
  if (gm2_install_rate(emu, GM2_EFFECT_RATE) != 0) {
    fprintf(stderr, "%s: cannot install %u Hz\n", who, GM2_EFFECT_RATE);
    gm2_destroy(emu);
    return NULL;
  }
  return emu;
}

int gm2_firmware_knob_entry(const gm2_emu *emu, uint32_t table, unsigned index,
                            const gm2_knob *knob, uint32_t *callback) {
  uint32_t entry = table + GM2_DESCRIPTOR_STRIDE * index;
  char name[GM2_DESCRIPTOR_NAME + 1u];
  for (unsigned word = 0; word < GM2_DESCRIPTOR_NAME; word++) {
    name[word] = (char)(gm2_read(emu, GM2_P, entry + word) & 0x7Fu);
  }
  unsigned length = GM2_DESCRIPTOR_NAME;
  while (length > 0u && name[length - 1u] == ' ') {
    length--;
  }
  name[length] = '\0';
  int32_t min = gm2_sign24(gm2_read(emu, GM2_P, entry + GM2_DESCRIPTOR_MIN));
  int32_t max = gm2_sign24(gm2_read(emu, GM2_P, entry + GM2_DESCRIPTOR_MAX));
  uint32_t address = gm2_read(emu, GM2_P, entry + GM2_DESCRIPTOR_CALLBACK);
  if (strcmp(name, knob->name) != 0) {
    fprintf(stderr, "descriptor entry %u is \"%s\", the knob table has \"%s\"\n", index, name,
            knob->name);
    return -1;
  }
  if (min != knob->min || max != knob->max) {
    fprintf(stderr, "%s is %d..%d in the descriptor, %d..%d in the knob table\n", knob->name, min,
            max, knob->min, knob->max);
    return -1;
  }
  if (address == 0u) {
    fprintf(stderr, "%s has no callback in the descriptor\n", knob->name);
    return -1;
  }
  *callback = address;
  return 0;
}

unsigned gm2_firmware_jobs(const gm2_emu *emu, uint32_t *record, unsigned cap) {
  const uint32_t head_word[2] = {GM2_JOB_QUEUE_HEAD, GM2_JOB_ACTIVE_HEAD};
  const uint32_t count_word[2] = {GM2_JOB_QUEUE_COUNT, GM2_JOB_ACTIVE_COUNT};
  const uint32_t count[2] = {gm2_read(emu, GM2_X, count_word[0]),
                             gm2_read(emu, GM2_X, count_word[1])};
  if (count[0] + count[1] > cap) {
    return 0;
  }
  unsigned written = 0;
  for (unsigned list = 0; list < 2u; list++) {
    uint32_t head = gm2_read(emu, GM2_X, head_word[list]);
    for (uint32_t i = 0; i < count[list] && head != 0u; i++) {
      record[written++] = head;
      head = gm2_read(emu, GM2_X, head);
    }
  }
  return written;
}

int gm2_firmware_read_jobs(const gm2_emu *emu, const gm2_firmware_job_spec *spec, uint32_t *record,
                           unsigned *count) {
  const unsigned held = gm2_firmware_jobs(emu, record, spec->cap);
  if (held == 0u) {
    fprintf(stderr, "%s: cannot read the block's job records\n", spec->name);
    return -1;
  }
  for (unsigned i = 0; i < held; i++) {
    const uint32_t handler = gm2_read(emu, GM2_X, record[i] + GM2_JOB_HANDLER);
    const uint32_t params = gm2_read(emu, GM2_X, record[i] + GM2_JOB_PARAMS);
    unsigned known = 0;
    for (unsigned h = 0; h < spec->handler_count; h++) {
      known += handler == spec->handlers[h];
    }
    const int payload = (params >= spec->data && params < spec->data + spec->data_words) ||
                        (spec->other_params != 0u && params == spec->other_params);
    if (known == 0u || !payload) {
      fprintf(stderr, "%s: job X:0x%06X runs P:0x%06X on X:0x%06X, not a record of the block\n",
              spec->name, record[i], handler, params);
      return -1;
    }
  }
  *count = held;
  return 0;
}

int gm2_firmware_check_jobs(const gm2_emu *emu, const gm2_firmware_job_spec *spec) {
  uint32_t record[GM2_FIRMWARE_JOBS_MAX];
  unsigned count = 0;
  if (gm2_firmware_read_jobs(emu, spec, record, &count) != 0) {
    return -1;
  }
  for (unsigned target = 0; target < spec->target_count; target++) {
    unsigned found = 0;
    for (unsigned i = 0; i < count && found == 0u; i++) {
      const uint32_t params = gm2_read(emu, GM2_X, record[i] + GM2_JOB_PARAMS);
      const uint32_t address = gm2_read(emu, GM2_X, params + GM2_JOB_DST);
      const uint32_t words = gm2_read(emu, GM2_X, params + GM2_JOB_COUNT);
      found = spec->targets[target] >= address && spec->targets[target] < address + words;
    }
    if (found == 0u) {
      fprintf(stderr, "%s: no record moves Y:0x%06X\n", spec->name, spec->targets[target]);
      return -1;
    }
  }
  return 0;
}

int gm2_firmware_check_same_jobs(const gm2_emu *emu, const gm2_firmware_job_spec *spec,
                                 const uint32_t *expected, unsigned expected_count) {
  uint32_t record[GM2_FIRMWARE_JOBS_MAX];
  unsigned count = 0;
  if (gm2_firmware_read_jobs(emu, spec, record, &count) != 0) {
    return -1;
  }
  if (count != expected_count) {
    fprintf(stderr, "%s: the block holds %u job records, %u after its activation\n", spec->name,
            count, expected_count);
    return -1;
  }
  for (unsigned i = 0; i < count; i++) {
    unsigned found = 0;
    for (unsigned j = 0; j < expected_count && found == 0u; j++) {
      found = record[i] == expected[j] ? 1u : 0u;
    }
    if (found == 0u) {
      fprintf(stderr, "%s: job X:0x%06X appeared after a knob turn and is never run\n", spec->name,
              record[i]);
      return -1;
    }
  }
  return 0;
}

int gm2_firmware_run_list(gm2_emu *emu, const uint32_t *record, unsigned count) {
  for (unsigned i = 0; i < count; i++) {
    if (gm2_firmware_run_job(emu, record[i]) != 0) {
      return -1;
    }
  }
  return 0;
}

int gm2_firmware_call(gm2_emu *emu, uint32_t target, int32_t budget) {
  gm2_set_reg(emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  return gm2_call(emu, target, budget, NULL) == GM2_CALL_OK ? 0 : -1;
}

void gm2_firmware_link(gm2_emu *emu, uint32_t address, uint32_t output) {
  const uint32_t link[2] = {GM2_STUB_ADDR + 2u, output};
  gm2_write_run(emu, GM2_Y, address, link, 2);
}

int gm2_firmware_run_job(gm2_emu *emu, uint32_t record) {
  uint32_t handler = gm2_read(emu, GM2_X, record + GM2_JOB_HANDLER);
  if (handler == 0u) {
    fprintf(stderr, "job record X:0x%06X names no handler\n", record);
    return -1;
  }
  /* P:0x0960B4 reads the check routine from P:(handler-1) behind its 0xD0BED0
   * signature word and calls it on the record's parameters; a handler without the
   * pair has no settled check. */
  gm2_set_reg(emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  gm2_set_reg(emu, DSP56300_REG_R0, record);
  if (gm2_call(emu, GM2_JOB_CHECK, GM2_FIRMWARE_CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "job record X:0x%06X: P:0x%06X did not return\n", record, GM2_JOB_CHECK);
    return -1;
  }
  gm2_set_reg(emu, DSP56300_REG_R0, gm2_read(emu, GM2_X, record + GM2_JOB_PARAMS));
  gm2_set_reg(emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  if (gm2_call(emu, handler, GM2_FIRMWARE_CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "job record X:0x%06X: P:0x%06X did not return\n", record, handler);
    return -1;
  }
  return 0;
}

int gm2_firmware_run_jobs(gm2_emu *emu, const char *who, unsigned cap) {
  uint32_t record[GM2_FIRMWARE_JOBS_MAX];
  if (cap > GM2_FIRMWARE_JOBS_MAX) {
    fprintf(stderr, "%s: job capacity %u is over the common maximum\n", who, cap);
    return -1;
  }
  const unsigned count = gm2_firmware_jobs(emu, record, cap);
  if (count == 0u) {
    fprintf(stderr, "%s: cannot read the block's job records\n", who);
    return -1;
  }
  return gm2_firmware_run_list(emu, record, count);
}

/* Every live Y word the records name, zero elsewhere in the fixed-size table. */
static int gm2_firmware_live_words(const gm2_emu *emu, const char *who,
                                   uint32_t live[GM2_FIRMWARE_JOBS_MAX][GM2_FIRMWARE_RECORD_WORDS],
                                   unsigned cap) {
  uint32_t record[GM2_FIRMWARE_JOBS_MAX];
  if (cap > GM2_FIRMWARE_JOBS_MAX) {
    fprintf(stderr, "%s: job capacity %u is over the common maximum\n", who, cap);
    return -1;
  }
  const unsigned count = gm2_firmware_jobs(emu, record, cap);
  if (count == 0u) {
    fprintf(stderr, "%s: cannot read the block's job records\n", who);
    return -1;
  }
  memset(live, 0, sizeof(uint32_t) * GM2_FIRMWARE_JOBS_MAX * GM2_FIRMWARE_RECORD_WORDS);
  for (unsigned i = 0; i < count; i++) {
    const uint32_t params = gm2_read(emu, GM2_X, record[i] + GM2_JOB_PARAMS);
    const uint32_t address = gm2_read(emu, GM2_X, params + GM2_JOB_DST);
    const uint32_t words = gm2_read(emu, GM2_X, params + GM2_JOB_COUNT);
    for (uint32_t word = 0; word < words && word < GM2_FIRMWARE_RECORD_WORDS; word++) {
      live[i][word] = gm2_read(emu, GM2_Y, address + word);
    }
  }
  return 0;
}

int gm2_firmware_settle_words(gm2_emu *emu, const char *who, unsigned cap) {
  uint32_t before[GM2_FIRMWARE_JOBS_MAX][GM2_FIRMWARE_RECORD_WORDS];
  uint32_t after[GM2_FIRMWARE_JOBS_MAX][GM2_FIRMWARE_RECORD_WORDS];
  if (gm2_firmware_live_words(emu, who, before, cap) != 0) {
    return -1;
  }
  for (unsigned pass = 0; pass < GM2_SETTLE_PASSES; pass++) {
    if (gm2_firmware_run_jobs(emu, who, cap) != 0 ||
        gm2_firmware_live_words(emu, who, after, cap) != 0) {
      return -1;
    }
    if (memcmp(before, after, sizeof before) == 0) {
      return 0;
    }
    memcpy(before, after, sizeof before);
  }
  fprintf(stderr, "%s: the jobs did not settle\n", who);
  return -1;
}

void gm2_firmware_audio_reset(gm2_emu *emu) {
  gm2_reset_regs(emu);
  for (unsigned reg = DSP56300_REG_M0; reg <= DSP56300_REG_M7; reg++) {
    gm2_set_reg(emu, reg, 0xFFFFFFu);
  }
  gm2_set_reg(emu, DSP56300_REG_SR, 0x800300u);
  gm2_set_reg(emu, DSP56300_REG_OMR, 0x004B01u);
}

int gm2_firmware_edit(gm2_emu *emu, unsigned algorithm, uint32_t callback, int value,
                      uint32_t stack) {
  gm2_set_reg(emu, DSP56300_REG_A2, 0);
  gm2_set_reg(emu, DSP56300_REG_A1, algorithm);
  gm2_set_reg(emu, DSP56300_REG_A0, 0);
  gm2_set_reg(emu, DSP56300_REG_B2, 0);
  gm2_set_reg(emu, DSP56300_REG_B1, (uint32_t)value & 0xFFFFFFu);
  gm2_set_reg(emu, DSP56300_REG_B0, 0);
  gm2_set_reg(emu, DSP56300_REG_R7, stack);
  return gm2_call(emu, callback, GM2_FIRMWARE_CALL_BUDGET, NULL) == GM2_CALL_OK ? 0 : -1;
}
