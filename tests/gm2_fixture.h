/* Fixture container: the firmware's answers plus the knob values that produced
 * them, so a file describes itself. Little-endian throughout.
 *
 *   16-byte header: "GM2FIX01", u32 rate, u32 section count
 *   per section:    u32 kind, u32 record size, u32 record count, records
 */
#ifndef GM2_FIXTURE_H
#define GM2_FIXTURE_H

#include <stddef.h>
#include <stdint.h>

#define GM2_FIXTURE_RATE 48000u
#define GM2_FIXTURE_NAME 24u

enum gm2_fixture_kind {
  GM2_FIX_KNOBS = 1,
  GM2_FIX_LEVELS = 2,
  GM2_FIX_SETUP = 3,
  GM2_FIX_SEGMENT = 4,
  GM2_FIX_OUTPUT = 5,
  GM2_FIX_SWF = 6,
  GM2_FIX_TABLE_INDEX = 7,
  GM2_FIX_TABLE_WORDS = 8,
  GM2_FIX_KINDS = 8
};

#define GM2_FIX_KNOBS_SIZE 36u
#define GM2_FIX_LEVELS_SIZE 12u
#define GM2_FIX_OUTPUT_SIZE 6u
#define GM2_FIX_SWF_SIZE 36u
#define GM2_FIX_TABLE_NAME 32u
#define GM2_FIX_TABLE_INDEX_SIZE 40u
#define GM2_FIX_TABLE_WORDS_SIZE 4u

/* One knob as the firmware descriptor prints it. */
typedef struct {
  char name[GM2_FIXTURE_NAME + 1u];
  int32_t min;
  int32_t max;
  int32_t def;
} gm2_fixture_knob;

/* One software-float routine, its operands and the answer. The 48-bit
 * conversions carry their integer in a.m/a.e or r.m/r.e as a low and a high
 * 24-bit word. */
typedef struct {
  uint32_t op;
  int32_t a_m, a_e;
  int32_t b_m, b_e;
  int32_t c_m, c_e;
  int32_t r_m, r_e;
} gm2_fixture_swf;

enum gm2_swf_op {
  GM2_SWF_ADD,
  GM2_SWF_SUB,
  GM2_SWF_MUL,
  GM2_SWF_DIV,
  GM2_SWF_NEG,
  GM2_SWF_CMP,
  GM2_SWF_MAC,
  GM2_SWF_MSUB,
  GM2_SWF_FROM_INT,
  GM2_SWF_TO_INT,
  GM2_SWF_ABS,
  GM2_SWF_EXP,
  GM2_SWF_LN,
  GM2_SWF_POW,
  GM2_SWF_SQRT,
  GM2_SWF_SINH,
  GM2_SWF_ATAN,
  GM2_SWF_TAN,
  GM2_SWF_SIN,
  GM2_SWF_COS,
  GM2_SWF_HZ20,
  GM2_SWF_DB_TO_LIN,
  GM2_SWF_TO_INT48,
  GM2_SWF_FROM_INT48,
  GM2_SWF_HZ80,
  GM2_SWF_DB,
  GM2_SWF_OPS
};

const char *gm2_swf_op_name(unsigned op);

/* One lookup table: its name and where its first word sits in the word section. */
typedef struct {
  char name[GM2_FIX_TABLE_NAME + 1u];
  uint32_t first;
  uint32_t count;
} gm2_fixture_table;

typedef struct {
  unsigned char *data;
  size_t bytes;
  uint32_t knobs;
  size_t offset[GM2_FIX_KINDS + 1u];
  uint32_t size[GM2_FIX_KINDS + 1u];
  uint32_t count[GM2_FIX_KINDS + 1u];
} gm2_fixture;

/* Reads and validates a whole file: magic, rate, every section's kind, size and
 * count, and that the records end exactly at the end of the file. 0 on success,
 * -1 with a message on stderr. */
int gm2_fixture_load(gm2_fixture *fixture, const char *path);
void gm2_fixture_free(gm2_fixture *fixture);

const void *gm2_fixture_records(const gm2_fixture *fixture, unsigned kind, uint32_t *count,
                                uint32_t *size);

/* Accumulates records in memory and writes them to <path>.tmp at the end, so a
 * failed run leaves the previous file alone. */
typedef struct {
  char *path;
  unsigned char *data[GM2_FIX_KINDS + 1u];
  size_t used[GM2_FIX_KINDS + 1u];
  size_t cap[GM2_FIX_KINDS + 1u];
  uint32_t size[GM2_FIX_KINDS + 1u];
  uint32_t count[GM2_FIX_KINDS + 1u];
} gm2_fixture_writer;

int gm2_fixture_writer_open(gm2_fixture_writer *writer, const char *path);
int gm2_fixture_writer_add(gm2_fixture_writer *writer, unsigned kind, uint32_t size,
                           const void *record);
int gm2_fixture_writer_close(gm2_fixture_writer *writer);
void gm2_fixture_writer_abort(gm2_fixture_writer *writer);

/* Record codecs. */
void gm2_fixture_put_i32(unsigned char *record, uint32_t words, const int32_t *values);
void gm2_fixture_get_i32(const void *record, uint32_t words, int32_t *values);
void gm2_fixture_put_segment(unsigned char *record, uint32_t knobs, const int32_t *values,
                             uint32_t frames);
void gm2_fixture_get_segment(const void *record, uint32_t knobs, int32_t *values, uint32_t *frames);
void gm2_fixture_put_knob(unsigned char *record, const gm2_fixture_knob *knob);
void gm2_fixture_get_knob(const void *record, gm2_fixture_knob *knob);
void gm2_fixture_put_output(unsigned char *record, int32_t left, int32_t right);
void gm2_fixture_get_output(const void *record, int32_t *left, int32_t *right);
void gm2_fixture_put_swf(unsigned char *record, const gm2_fixture_swf *value);
void gm2_fixture_get_swf(const void *record, gm2_fixture_swf *value);
void gm2_fixture_put_table(unsigned char *record, const gm2_fixture_table *table);
void gm2_fixture_get_table(const void *record, gm2_fixture_table *table);
void gm2_fixture_put_word(unsigned char *record, int32_t value);
int32_t gm2_fixture_get_word(const void *record);

/* The 48-bit integers the 48-bit conversions carry in two record words. */
static inline int64_t gm2_fixture_int48(int32_t low, int32_t high) {
  uint64_t value = ((uint64_t)(uint32_t)high << 24) | ((uint64_t)(uint32_t)low & 0xFFFFFFu);
  return (value & 0x800000000000ull) != 0 ? (int64_t)(value | 0xFFFF000000000000ull)
                                          : (int64_t)value;
}

static inline void gm2_fixture_split48(int64_t value, int32_t *low, int32_t *high) {
  *low = (int32_t)((uint64_t)value & 0xFFFFFFu);
  *high = (int32_t)(((uint64_t)value >> 24) & 0xFFFFFFu);
}

#endif /* GM2_FIXTURE_H */
