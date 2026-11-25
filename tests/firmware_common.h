/* Boot, descriptor reads and the job lists shared by the firmware adapters. */
#ifndef GM2_FIRMWARE_COMMON_H
#define GM2_FIRMWARE_COMMON_H

#include "gm2_effect.h"
#include "gm2_emu.h"

#include <stdint.h>

#define GM2_FIRMWARE_DATA_DIR "reverse_engineering/firmware"
#define GM2_FIRMWARE_CALL_BUDGET 4000000
#define GM2_FIRMWARE_JOBS_MAX 16u
#define GM2_FIRMWARE_RECORD_WORDS 8u

/* The job record's two fields a runner reads: {next record, flag, these two}. */
#define GM2_JOB_PARAMS 2u  /* X: the record naming the job's parameters */
#define GM2_JOB_HANDLER 3u /* X: the routine that runs it */
#define GM2_JOB_DST 1u
#define GM2_JOB_COUNT 2u
#define GM2_JOB_LINEAR 0x0001B9u
#define GM2_JOB_SMOOTH 0x0001FDu
#define GM2_JOB_GROUP 0x00026Fu

/* A fresh image at GM2_EFFECT_RATE: application loaded, initializer applied,
 * registers reset and the rate installed. NULL on failure, after a message on
 * stderr naming `who`. */
gm2_emu *gm2_firmware_boot(const char *who);

/* Descriptor entry `index` of the table at P:`table` against `knob`: its name
 * (ten words, one character each, trailing spaces trimmed), its clamp window and
 * its callback. 0 when all of them match, -1 after naming the field that
 * differs. */
int gm2_firmware_knob_entry(const gm2_emu *emu, uint32_t table, unsigned index,
                            const gm2_knob *knob, uint32_t *callback);

/* The job records the firmware holds: the background queue (head X:0x0993C3, count
 * X:0x0993C0), then the active list (head X:0x0200, count X:0x0993C1). A record is
 * {next record, flag, parameters, handler} and its first word closes the ring back
 * on the list's head, which the count bounds. The harness never drains the queue
 * into the active list, so both are returned. Returns the records written, 0 when
 * `cap` is too small. */
unsigned gm2_firmware_jobs(const gm2_emu *emu, uint32_t *record, unsigned cap);

/* One block's job-record contract: the handlers its records name, the X payload
 * window they run on, an optional second payload, and the live Y words its
 * records must move. */
typedef struct {
  const char *name;
  const uint32_t *handlers;
  unsigned handler_count;
  uint32_t data;
  unsigned data_words;
  uint32_t other_params; /* a level record outside data, or 0 */
  const uint32_t *targets; /* required live Y locations, or NULL */
  unsigned target_count;
  unsigned cap;
} gm2_firmware_job_spec;

/* The held records, up to `spec->cap`, with every record's handler and payload
 * checked against `spec`. Writes the held count. */
int gm2_firmware_read_jobs(const gm2_emu *emu, const gm2_firmware_job_spec *spec, uint32_t *record,
                           unsigned *count);

/* The held records checked as `read_jobs` does, then every `spec->targets` word
 * required to lie in one record's destination range. */
int gm2_firmware_check_jobs(const gm2_emu *emu, const gm2_firmware_job_spec *spec);

/* The held records checked as `read_jobs` does, then required to be exactly the
 * records in `expected`, in any order. */
int gm2_firmware_check_same_jobs(const gm2_emu *emu, const gm2_firmware_job_spec *spec,
                                 const uint32_t *expected, unsigned expected_count);

/* Each record run once through `gm2_firmware_run_job`, in order. */
int gm2_firmware_run_list(gm2_emu *emu, const uint32_t *record, unsigned count);

/* Call `target` with R7 on the device stack; 0 when it returns. */
int gm2_firmware_call(gm2_emu *emu, uint32_t target, int32_t budget);

/* Write the two Y link words {GM2_STUB_ADDR + 2, output} at `address`. */
void gm2_firmware_link(gm2_emu *emu, uint32_t address, uint32_t output);

/* True when the descriptor's format word asks for a callback's second call. */
static inline int gm2_firmware_second_call(uint32_t format) {
  return (format & 0x800000u) != 0u && (format & 0x0002000u) != 0u;
}

/* One record, run as the list manager P:0x0960C6 and the runner P:0x0001CD run
 * it: the record's settled check through P:0x0960B4, then R0 = its parameters and
 * its handler. 0 on success, -1 after a message on stderr. */
int gm2_firmware_run_job(gm2_emu *emu, uint32_t record);

/* Every record, run once, with at most `cap` records accepted. */
int gm2_firmware_run_jobs(gm2_emu *emu, const char *who, unsigned cap);

/* Every live Y word the records name, zero elsewhere in the fixed-size table. */
int gm2_firmware_live_words(const gm2_emu *emu, const char *who,
                            uint32_t live[GM2_FIRMWARE_JOBS_MAX][GM2_FIRMWARE_RECORD_WORDS],
                            unsigned cap);

/* Run the jobs until none of those words moves, the rest state a native instance
 * starts in. */
int gm2_firmware_settle_words(gm2_emu *emu, const char *who, unsigned cap);

/* The audio continuations' common register state, before their task pointer and
 * record are installed. */
void gm2_firmware_audio_reset(gm2_emu *emu);

/* One descriptor callback with the edit registers P:0x083268 passes. The caller
 * writes the raw slot and reports a callback that does not return. */
int gm2_firmware_edit(gm2_emu *emu, unsigned algorithm, uint32_t callback, int value,
                      uint32_t stack);

#endif /* GM2_FIRMWARE_COMMON_H */
