#include "gm2_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_BYTES 16u
#define SECTION_BYTES 12u

static const char k_magic[8] = {'G', 'M', '2', 'F', 'I', 'X', '0', '1'};

static uint32_t get_u32(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_u32(unsigned char *p, uint32_t value) {
  p[0] = (unsigned char)(value & 0xFFu);
  p[1] = (unsigned char)((value >> 8) & 0xFFu);
  p[2] = (unsigned char)((value >> 16) & 0xFFu);
  p[3] = (unsigned char)((value >> 24) & 0xFFu);
}

static int32_t get_i32(const unsigned char *p) { return (int32_t)get_u32(p); }

static unsigned fixed_size(unsigned kind) {
  switch (kind) {
  case GM2_FIX_KNOBS:
    return GM2_FIX_KNOBS_SIZE;
  case GM2_FIX_LEVELS:
    return GM2_FIX_LEVELS_SIZE;
  case GM2_FIX_OUTPUT:
    return GM2_FIX_OUTPUT_SIZE;
  case GM2_FIX_SWF:
    return GM2_FIX_SWF_SIZE;
  case GM2_FIX_TABLE_INDEX:
    return GM2_FIX_TABLE_INDEX_SIZE;
  case GM2_FIX_TABLE_WORDS:
    return GM2_FIX_TABLE_WORDS_SIZE;
  default:
    return 0;
  }
}

const char *gm2_swf_op_name(unsigned op) {
  static const char *const k_names[GM2_SWF_OPS] = {
      "add",    "sub", "mul",  "div",       "neg",      "cmp",        "mac",  "msub", "from_int",
      "to_int", "abs", "exp",  "ln",        "pow",      "sqrt",       "sinh", "atan", "tan",
      "sin",    "cos", "hz20", "db_to_lin", "to_int48", "from_int48", "hz80", "db"};
  return op < GM2_SWF_OPS ? k_names[op] : "unknown";
}

int gm2_fixture_load(gm2_fixture *fixture, const char *path) {
  memset(fixture, 0, sizeof *fixture);
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "gm2: cannot open %s\n", path);
    return -1;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return -1;
  }
  long length = ftell(file);
  if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return -1;
  }
  size_t bytes = (size_t)length;
  unsigned char *data = malloc(bytes + 1u);
  if (data == NULL || fread(data, 1, bytes, file) != bytes) {
    fprintf(stderr, "gm2: cannot read %s\n", path);
    free(data);
    fclose(file);
    return -1;
  }
  fclose(file);
  fixture->data = data;
  fixture->bytes = bytes;

  if (bytes < HEADER_BYTES || memcmp(data, k_magic, sizeof k_magic) != 0) {
    fprintf(stderr, "gm2: %s is not a fixture file\n", path);
    goto fail;
  }
  if (get_u32(data + 8) != GM2_FIXTURE_RATE) {
    fprintf(stderr, "gm2: %s holds a rate of %u, not %u\n", path, get_u32(data + 8),
            GM2_FIXTURE_RATE);
    goto fail;
  }
  uint32_t sections = get_u32(data + 12);
  size_t offset = HEADER_BYTES;
  unsigned previous = 0;
  for (uint32_t s = 0; s < sections; s++) {
    if (offset + SECTION_BYTES > bytes) {
      fprintf(stderr, "gm2: %s is truncated in section %u\n", path, s);
      goto fail;
    }
    unsigned kind = get_u32(data + offset);
    uint32_t size = get_u32(data + offset + 4);
    uint32_t count = get_u32(data + offset + 8);
    offset += SECTION_BYTES;
    if (kind <= previous || kind > GM2_FIX_KINDS) {
      fprintf(stderr, "gm2: %s section %u has kind %u\n", path, s, kind);
      goto fail;
    }
    previous = kind;
    unsigned fixed = fixed_size(kind);
    if (fixed != 0u && size != fixed) {
      fprintf(stderr, "gm2: %s kind %u has record size %u, not %u\n", path, kind, size, fixed);
      goto fail;
    }
    if (kind == GM2_FIX_KNOBS) {
      if (count == 0u) {
        fprintf(stderr, "gm2: %s has no knobs\n", path);
        goto fail;
      }
      fixture->knobs = count;
    } else if (kind == GM2_FIX_SETUP) {
      if (fixture->knobs == 0u || size % 4u != 0u || size / 4u <= fixture->knobs) {
        fprintf(stderr, "gm2: %s setup record is %u bytes\n", path, size);
        goto fail;
      }
    } else if (kind == GM2_FIX_SEGMENT) {
      if (fixture->knobs == 0u || size != 4u * (fixture->knobs + 1u)) {
        fprintf(stderr, "gm2: %s segment record is %u bytes\n", path, size);
        goto fail;
      }
    } else if (kind == GM2_FIX_LEVELS && count != 1u) {
      fprintf(stderr, "gm2: %s holds %u level records\n", path, count);
      goto fail;
    }
    size_t records = (size_t)size * (size_t)count;
    if (size != 0u && count != 0u && records / (size_t)count != (size_t)size) {
      fprintf(stderr, "gm2: %s section %u overflows\n", path, s);
      goto fail;
    }
    if (records > bytes - offset) {
      fprintf(stderr, "gm2: %s is truncated in section %u\n", path, s);
      goto fail;
    }
    fixture->offset[kind] = offset;
    fixture->size[kind] = size;
    fixture->count[kind] = count;
    offset += records;
  }
  if (offset != bytes) {
    fprintf(stderr, "gm2: %s has %zu trailing bytes\n", path, bytes - offset);
    goto fail;
  }
  if (fixture->count[GM2_FIX_TABLE_INDEX] != 0u) {
    if (fixture->count[GM2_FIX_TABLE_WORDS] == 0u) {
      fprintf(stderr, "gm2: %s indexes tables without their words\n", path);
      goto fail;
    }
    for (uint32_t record = 0; record < fixture->count[GM2_FIX_TABLE_INDEX]; record++) {
      gm2_fixture_table table;
      gm2_fixture_get_table(fixture->data + fixture->offset[GM2_FIX_TABLE_INDEX] +
                                (size_t)fixture->size[GM2_FIX_TABLE_INDEX] * record,
                            &table);
      if (table.count == 0u || table.first > fixture->count[GM2_FIX_TABLE_WORDS] ||
          table.count > fixture->count[GM2_FIX_TABLE_WORDS] - table.first) {
        fprintf(stderr, "gm2: %s table %u (%.32s) is outside the word section\n", path, record,
                table.name);
        goto fail;
      }
    }
  }
  return 0;

fail:
  gm2_fixture_free(fixture);
  return -1;
}

void gm2_fixture_free(gm2_fixture *fixture) {
  free(fixture->data);
  fixture->data = NULL;
  fixture->bytes = 0;
}

const void *gm2_fixture_records(const gm2_fixture *fixture, unsigned kind, uint32_t *count,
                                uint32_t *size) {
  if (kind == 0u || kind > GM2_FIX_KINDS || fixture->size[kind] == 0u) {
    return NULL;
  }
  if (count != NULL) {
    *count = fixture->count[kind];
  }
  if (size != NULL) {
    *size = fixture->size[kind];
  }
  return fixture->data + fixture->offset[kind];
}

int gm2_fixture_writer_open(gm2_fixture_writer *writer, const char *path) {
  memset(writer, 0, sizeof *writer);
  size_t length = strlen(path);
  writer->path = malloc(length + 1u);
  if (writer->path == NULL) {
    return -1;
  }
  memcpy(writer->path, path, length + 1u);
  return 0;
}

int gm2_fixture_writer_add(gm2_fixture_writer *writer, unsigned kind, uint32_t size,
                           const void *record) {
  if (kind == 0u || kind > GM2_FIX_KINDS || size == 0u) {
    return -1;
  }
  size_t *used = &writer->used[kind];
  if (writer->size[kind] != 0u && writer->size[kind] != size) {
    return -1;
  }
  writer->size[kind] = size;
  if (*used + size > writer->cap[kind]) {
    size_t cap = writer->cap[kind] == 0u ? 4096u : writer->cap[kind];
    while (cap < *used + size) {
      cap *= 2u;
    }
    unsigned char *grown = realloc(writer->data[kind], cap);
    if (grown == NULL) {
      return -1;
    }
    writer->data[kind] = grown;
    writer->cap[kind] = cap;
  }
  memcpy(writer->data[kind] + *used, record, size);
  *used += size;
  writer->count[kind]++;
  return 0;
}

int gm2_fixture_writer_close(gm2_fixture_writer *writer) {
  if (writer->path == NULL) {
    return -1;
  }
  size_t length = strlen(writer->path);
  char *tmp = malloc(length + sizeof ".tmp");
  int status = -1;
  if (tmp == NULL) {
    goto done;
  }
  memcpy(tmp, writer->path, length);
  memcpy(tmp + length, ".tmp", sizeof ".tmp");
  FILE *file = fopen(tmp, "wb");
  if (file == NULL) {
    free(tmp);
    goto done;
  }
  unsigned char header[HEADER_BYTES];
  uint32_t sections = 0;
  for (unsigned kind = 1; kind <= GM2_FIX_KINDS; kind++) {
    if (writer->count[kind] != 0u) {
      sections++;
    }
  }
  memcpy(header, k_magic, sizeof k_magic);
  put_u32(header + 8, GM2_FIXTURE_RATE);
  put_u32(header + 12, sections);
  int ok = fwrite(header, 1, sizeof header, file) == sizeof header;
  for (unsigned kind = 1; ok && kind <= GM2_FIX_KINDS; kind++) {
    if (writer->count[kind] == 0u) {
      continue;
    }
    unsigned char section[SECTION_BYTES];
    put_u32(section, kind);
    put_u32(section + 4, writer->size[kind]);
    put_u32(section + 8, writer->count[kind]);
    ok = fwrite(section, 1, sizeof section, file) == sizeof section &&
         fwrite(writer->data[kind], 1, writer->used[kind], file) == writer->used[kind];
  }
  ok = fclose(file) == 0 && ok;
  if (ok && rename(tmp, writer->path) != 0) {
    ok = 0;
  }
  if (!ok) {
    remove(tmp);
  }
  free(tmp);
  status = ok ? 0 : -1;
done:
  gm2_fixture_writer_abort(writer);
  return status;
}

void gm2_fixture_writer_abort(gm2_fixture_writer *writer) {
  for (unsigned kind = 1; kind <= GM2_FIX_KINDS; kind++) {
    free(writer->data[kind]);
    writer->data[kind] = NULL;
  }
  free(writer->path);
  writer->path = NULL;
  memset(writer->used, 0, sizeof writer->used);
  memset(writer->cap, 0, sizeof writer->cap);
  memset(writer->size, 0, sizeof writer->size);
  memset(writer->count, 0, sizeof writer->count);
}

void gm2_fixture_put_i32(unsigned char *record, uint32_t words, const int32_t *values) {
  for (uint32_t i = 0; i < words; i++) {
    put_u32(record + 4u * i, (uint32_t)values[i]);
  }
}

void gm2_fixture_get_i32(const void *record, uint32_t words, int32_t *values) {
  const unsigned char *bytes = record;
  for (uint32_t i = 0; i < words; i++) {
    values[i] = get_i32(bytes + 4u * i);
  }
}

void gm2_fixture_put_segment(unsigned char *record, uint32_t knobs, const int32_t *values,
                             uint32_t frames) {
  gm2_fixture_put_i32(record, knobs, values);
  put_u32(record + 4u * knobs, frames);
}

void gm2_fixture_get_segment(const void *record, uint32_t knobs, int32_t *values,
                             uint32_t *frames) {
  if (values != NULL) {
    gm2_fixture_get_i32(record, knobs, values);
  }
  if (frames != NULL) {
    *frames = get_u32((const unsigned char *)record + 4u * knobs);
  }
}

void gm2_fixture_put_knob(unsigned char *record, const gm2_fixture_knob *knob) {
  memset(record, 0, GM2_FIX_KNOBS_SIZE);
  size_t length = strlen(knob->name);
  if (length >= GM2_FIXTURE_NAME) {
    length = GM2_FIXTURE_NAME - 1u;
  }
  memcpy(record, knob->name, length);
  put_u32(record + GM2_FIXTURE_NAME, (uint32_t)knob->min);
  put_u32(record + GM2_FIXTURE_NAME + 4u, (uint32_t)knob->max);
  put_u32(record + GM2_FIXTURE_NAME + 8u, (uint32_t)knob->def);
}

void gm2_fixture_get_knob(const void *record, gm2_fixture_knob *knob) {
  const unsigned char *bytes = record;
  memcpy(knob->name, bytes, GM2_FIXTURE_NAME);
  knob->name[GM2_FIXTURE_NAME] = '\0';
  knob->min = get_i32(bytes + GM2_FIXTURE_NAME);
  knob->max = get_i32(bytes + GM2_FIXTURE_NAME + 4u);
  knob->def = get_i32(bytes + GM2_FIXTURE_NAME + 8u);
}

static int32_t word24(const unsigned char *p) {
  uint32_t value = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
  return (value & 0x800000u) != 0u ? (int32_t)(value - 0x1000000u) : (int32_t)value;
}

static void put_word24(unsigned char *p, int32_t value) {
  uint32_t word = (uint32_t)value & 0xFFFFFFu;
  p[0] = (unsigned char)(word & 0xFFu);
  p[1] = (unsigned char)((word >> 8) & 0xFFu);
  p[2] = (unsigned char)((word >> 16) & 0xFFu);
}

void gm2_fixture_put_output(unsigned char *record, int32_t left, int32_t right) {
  put_word24(record, left);
  put_word24(record + 3, right);
}

void gm2_fixture_get_output(const void *record, int32_t *left, int32_t *right) {
  const unsigned char *bytes = record;
  *left = word24(bytes);
  *right = word24(bytes + 3);
}

void gm2_fixture_put_swf(unsigned char *record, const gm2_fixture_swf *value) {
  put_u32(record + 0, value->op);
  put_u32(record + 4, (uint32_t)value->a_m);
  put_u32(record + 8, (uint32_t)value->a_e);
  put_u32(record + 12, (uint32_t)value->b_m);
  put_u32(record + 16, (uint32_t)value->b_e);
  put_u32(record + 20, (uint32_t)value->c_m);
  put_u32(record + 24, (uint32_t)value->c_e);
  put_u32(record + 28, (uint32_t)value->r_m);
  put_u32(record + 32, (uint32_t)value->r_e);
}

void gm2_fixture_get_swf(const void *record, gm2_fixture_swf *value) {
  const unsigned char *bytes = record;
  value->op = get_u32(bytes + 0);
  value->a_m = get_i32(bytes + 4);
  value->a_e = get_i32(bytes + 8);
  value->b_m = get_i32(bytes + 12);
  value->b_e = get_i32(bytes + 16);
  value->c_m = get_i32(bytes + 20);
  value->c_e = get_i32(bytes + 24);
  value->r_m = get_i32(bytes + 28);
  value->r_e = get_i32(bytes + 32);
}

void gm2_fixture_put_table(unsigned char *record, const gm2_fixture_table *table) {
  memset(record, 0, GM2_FIX_TABLE_INDEX_SIZE);
  size_t length = strlen(table->name);
  if (length >= GM2_FIX_TABLE_NAME) {
    length = GM2_FIX_TABLE_NAME - 1u;
  }
  memcpy(record, table->name, length);
  put_u32(record + GM2_FIX_TABLE_NAME, table->first);
  put_u32(record + GM2_FIX_TABLE_NAME + 4u, table->count);
}

void gm2_fixture_get_table(const void *record, gm2_fixture_table *table) {
  const unsigned char *bytes = record;
  memcpy(table->name, bytes, GM2_FIX_TABLE_NAME);
  table->name[GM2_FIX_TABLE_NAME] = '\0';
  table->first = get_u32(bytes + GM2_FIX_TABLE_NAME);
  table->count = get_u32(bytes + GM2_FIX_TABLE_NAME + 4u);
}

void gm2_fixture_put_word(unsigned char *record, int32_t value) {
  put_word24(record, value);
  record[3] = 0;
}

int32_t gm2_fixture_get_word(const void *record) { return word24(record); }
