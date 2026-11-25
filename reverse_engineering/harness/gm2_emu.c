#include "gm2_emu.h"

#include <dsp56300.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GM2_OP_JSR 0x0BF080u
#define GM2_OP_STOP 0x000087u
#define GM2_INIT_MAX_DESC 64

/* AAR0 = 0x0805B9 maps this window to one external SRAM visible from X and Y. */
#define GM2_EXT_SRAM_LO 0x080000u
#define GM2_EXT_SRAM_HI 0x0A0000u

struct gm2_emu {
  Dsp56300Jit *dsp;
  /* Y uses three regions (below, inside and above [GM2_EXT_SRAM_LO, GM2_EXT_SRAM_HI)),
   * X and P one each. */
  Dsp56300MemoryRegion regions[5];
  uint32_t *mem[3];
  int stub_valid;
  uint32_t stub_target;
};

static const Dsp56300MemSpace k_ffi_space[3] = {DSP56300_MEM_SPACE_X, DSP56300_MEM_SPACE_Y,
                                                DSP56300_MEM_SPACE_P};

#define GM2_MANIFEST_LINE 512u

static const char k_manifest_header[] = "filename,load_address,words,bytes,kind";

uint32_t gm2_fill_x_from_p(gm2_emu *emu, uint32_t addr, uint32_t words) {
  uint32_t filled = 0;
  for (uint32_t i = 0; i < words; i++) {
    uint32_t a = addr + i;
    if (a >= GM2_MEM_WORDS || gm2_read(emu, GM2_X, a) != 0) {
      continue;
    }
    uint32_t value = gm2_read(emu, GM2_P, a);
    if (value != 0) {
      gm2_write(emu, GM2_X, a, value);
      filled++;
    }
  }
  return filled;
}

int gm2_install_rate(gm2_emu *emu, unsigned rate) {
  gm2_set_reg(emu, DSP56300_REG_A2, 0);
  gm2_set_reg(emu, DSP56300_REG_A1, rate);
  gm2_set_reg(emu, DSP56300_REG_A0, 0);
  if (gm2_call(emu, 0x000BFDu, 4000, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "gm2: integer to float P:0x000BFD did not return\n");
    return -1;
  }
  gm2_set_reg(emu, DSP56300_REG_R0, 0x099429u);
  if (gm2_call(emu, 0x082DC0u, 20000, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "gm2: rate install P:0x082DC0 did not return\n");
    return -1;
  }
  return 0;
}

int gm2_mirror_descriptor(gm2_emu *emu, unsigned algorithm) {
  uint32_t rec = gm2_read(emu, GM2_X, GM2_CHAIN + algorithm);
  uint32_t count = gm2_read(emu, GM2_P, rec + 1);
  uint32_t table = gm2_read(emu, GM2_P, rec + 7);
  if (rec == 0 || count == 0 || table == 0) {
    fprintf(stderr, "gm2: descriptor X:0x%06X has no P-side record\n", GM2_CHAIN + algorithm);
    return -1;
  }
  /* Record plus the parameter table the accessor walks with a 15-word stride. */
  uint32_t lo = rec < table ? rec : table;
  uint32_t hi = table + 15u * count - 1u;
  if (hi < rec + 7u) {
    hi = rec + 7u;
  }
  return (int)gm2_fill_x_from_p(emu, lo, hi - lo + 1u);
}

int gm2_filter_subtype(gm2_emu *emu, unsigned subtype) {
  if (subtype > 6u) {
    fprintf(stderr, "gm2: filter subtype %u is outside 0..6\n", subtype);
    return -1;
  }
  uint32_t record = subtype <= 2u ? 0x085326u : subtype <= 4u ? 0x08504Eu : 0x084E66u;
  uint32_t entry = GM2_FILTER_SUBTYPES + 3u * subtype;
  if (gm2_read(emu, GM2_P, record + 2u) != gm2_read(emu, GM2_P, entry)) {
    fprintf(stderr, "gm2: sub-record P:0x%06X does not run subtype %u\n", record, subtype);
    return -1;
  }
  uint32_t table = gm2_read(emu, GM2_P, entry + 1u);
  gm2_write(emu, GM2_X, GM2_CHAIN + 1u, record);
  gm2_fill_x_from_p(emu, record, 8u);
  gm2_write(emu, GM2_X, record + 7u, table);
  /* Indices 0..15, the hidden kill flag included. */
  gm2_fill_x_from_p(emu, table, 16u * 15u);
  return 0;
}

int gm2_delay_subtype(gm2_emu *emu, unsigned subtype) {
  if (subtype > 3u) {
    fprintf(stderr, "gm2: delay subtype %u is outside 0..3\n", subtype);
    return -1;
  }
  uint32_t record = gm2_read(emu, GM2_X, GM2_CHAIN + 4u);
  if (record != 0x086275u) {
    fprintf(stderr, "gm2: delay record X:0x%06X is not 0x086275\n", record);
    return -1;
  }
  gm2_fill_x_from_p(emu, record, 8u);
  for (unsigned s = 0; s < 4u; s++) {
    uint32_t tbl = gm2_read(emu, GM2_X, 0x098ED5u + s);
    gm2_fill_x_from_p(emu, tbl, 16u * 15u);
  }
  uint32_t table = gm2_read(emu, GM2_X, 0x098ED5u + subtype);
  gm2_write(emu, GM2_P, record + 7u, table);
  gm2_write(emu, GM2_X, record + 7u, table);
  return 0;
}

int gm2_mod_subtype(gm2_emu *emu, unsigned subtype) {
  if (subtype > 12u) {
    fprintf(stderr, "gm2: modulation subtype %u is outside 0..12\n", subtype);
    return -1;
  }
  uint32_t record = subtype <= 8u ? 0x085EADu : subtype <= 10u ? 0x08562Du : 0x085815u;
  uint32_t entry = GM2_MOD_SUBTYPES + 3u * subtype;
  if (gm2_read(emu, GM2_P, record + 2u) != gm2_read(emu, GM2_P, entry)) {
    fprintf(stderr, "gm2: sub-record P:0x%06X does not run subtype %u\n", record, subtype);
    return -1;
  }
  uint32_t table = gm2_read(emu, GM2_P, entry + 1u);
  gm2_write(emu, GM2_X, GM2_CHAIN + 3u, record);
  gm2_fill_x_from_p(emu, 0x085445u, 8u);
  gm2_fill_x_from_p(emu, record, 8u);
  gm2_fill_x_from_p(emu, 0x085355u, 16u * 15u);
  for (unsigned s = 0; s <= 12u; s++) {
    uint32_t tbl = gm2_read(emu, GM2_P, GM2_MOD_SUBTYPES + 3u * s + 1u);
    gm2_fill_x_from_p(emu, tbl, 16u * 15u);
  }
  gm2_write(emu, GM2_P, 0x08544Cu, table);
  gm2_write(emu, GM2_X, 0x08544Cu, table);
  gm2_write(emu, GM2_P, record + 7u, table);
  gm2_write(emu, GM2_X, record + 7u, table);
  return 0;
}

int gm2_pitch_subtype(gm2_emu *emu, unsigned subtype) {
  if (subtype > 4u) {
    fprintf(stderr, "gm2: pitch subtype %u is outside 0..4\n", subtype);
    return -1;
  }
  uint32_t record = gm2_read(emu, GM2_X, GM2_CHAIN + 2u);
  if (record != 0x084B71u) {
    fprintf(stderr, "gm2: pitch record X:0x%06X is not 0x084B71\n", record);
    return -1;
  }
  gm2_fill_x_from_p(emu, record, 8u);
  for (unsigned s = 0; s < 5u; s++) {
    uint32_t tbl = gm2_read(emu, GM2_X, 0x098853u + s);
    gm2_fill_x_from_p(emu, tbl, 16u * 15u);
  }
  uint32_t table = gm2_read(emu, GM2_X, 0x098853u + subtype);
  gm2_write(emu, GM2_P, record + 7u, table);
  gm2_write(emu, GM2_X, record + 7u, table);
  gm2_write(emu, GM2_X, 0x099A61u, 440u);
  gm2_write(emu, GM2_X, 0x099A63u, 0u);
  return 0;
}

gm2_emu *gm2_create(void) {
  gm2_emu *emu = calloc(1, sizeof *emu);
  if (emu == NULL) {
    return NULL;
  }
  for (int i = 0; i < 3; i++) {
    emu->mem[i] = calloc(GM2_MEM_WORDS, sizeof *emu->mem[i]);
    if (emu->mem[i] == NULL) {
      gm2_destroy(emu);
      return NULL;
    }
  }
  /* The FFI maps [start, end) and indexes the buffer from `start`, so a region
   * that starts partway through a buffer gets a base past that point. */
  emu->regions[0] = (Dsp56300MemoryRegion){
      .start = 0,
      .end = GM2_MEM_WORDS,
      .kind = DSP56300_REGION_BUFFER,
      .data = {.buffer = {.base = emu->mem[GM2_X], .offset = 0}},
  };
  emu->regions[1] = (Dsp56300MemoryRegion){
      .start = 0,
      .end = GM2_EXT_SRAM_LO,
      .kind = DSP56300_REGION_BUFFER,
      .data = {.buffer = {.base = emu->mem[GM2_Y], .offset = 0}},
  };
  emu->regions[2] = (Dsp56300MemoryRegion){
      .start = GM2_EXT_SRAM_LO,
      .end = GM2_EXT_SRAM_HI,
      .kind = DSP56300_REGION_BUFFER,
      .data = {.buffer = {.base = emu->mem[GM2_X] + GM2_EXT_SRAM_LO, .offset = 0}},
  };
  emu->regions[3] = (Dsp56300MemoryRegion){
      .start = GM2_EXT_SRAM_HI,
      .end = GM2_MEM_WORDS,
      .kind = DSP56300_REGION_BUFFER,
      .data = {.buffer = {.base = emu->mem[GM2_Y] + GM2_EXT_SRAM_HI, .offset = 0}},
  };
  emu->regions[4] = (Dsp56300MemoryRegion){
      .start = 0,
      .end = GM2_MEM_WORDS,
      .kind = DSP56300_REGION_BUFFER,
      .data = {.buffer = {.base = emu->mem[GM2_P], .offset = 0}},
  };
  Dsp56300CreateInfo info;
  memset(&info, 0, sizeof info);
  info.memory_map.x_regions = &emu->regions[0];
  info.memory_map.x_count = 1;
  info.memory_map.y_regions = &emu->regions[1];
  info.memory_map.y_count = 3;
  info.memory_map.p_regions = &emu->regions[4];
  info.memory_map.p_count = 1;
  emu->dsp = dsp56300_create(&info);
  if (emu->dsp == NULL) {
    gm2_destroy(emu);
    return NULL;
  }
  return emu;
}

void gm2_destroy(gm2_emu *emu) {
  if (emu == NULL) {
    return;
  }
  dsp56300_destroy(emu->dsp);
  for (int i = 0; i < 3; i++) {
    free(emu->mem[i]);
  }
  free(emu);
}

// cppcheck-suppress constParameterPointer -- hands out writable memory
uint32_t *gm2_raw(gm2_emu *emu, enum gm2_space sp) { return emu->mem[sp]; }

uint32_t gm2_read(const gm2_emu *emu, enum gm2_space sp, uint32_t addr) {
  if (addr >= GM2_MEM_WORDS) {
    return 0;
  }
  if (sp == GM2_Y && addr >= GM2_EXT_SRAM_LO && addr < GM2_EXT_SRAM_HI) {
    return emu->mem[GM2_X][addr];
  }
  return emu->mem[sp][addr];
}

void gm2_write(gm2_emu *emu, enum gm2_space sp, uint32_t addr, uint32_t value) {
  if (addr >= GM2_MEM_WORDS) {
    return;
  }
  /* Goes through the JIT so P-word changes invalidate translated blocks. */
  dsp56300_write_memory(emu->dsp, k_ffi_space[sp], addr, value & 0xFFFFFFu);
}

void gm2_write_run(gm2_emu *emu, enum gm2_space sp, uint32_t addr, const uint32_t *in,
                   uint32_t words) {
  dsp56300_write_memory_run(emu->dsp, k_ffi_space[sp], addr, in, words);
}

static int read_u24be(const char *path, uint32_t **out, uint32_t *out_words) {
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    fprintf(stderr, "gm2: cannot open %s: %s\n", path, strerror(errno));
    return -1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  long size = ftell(f);
  if (size <= 0 || size % 3 != 0) {
    fprintf(stderr, "gm2: %s is not a whole number of 24-bit words\n", path);
    fclose(f);
    return -1;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fprintf(stderr, "gm2: cannot rewind %s: %s\n", path, strerror(errno));
    fclose(f);
    return -1;
  }
  size_t bytes = (size_t)size;
  unsigned char *buf = malloc(bytes);
  uint32_t *words = malloc((bytes / 3) * sizeof *words);
  if (buf == NULL || words == NULL || fread(buf, 1, bytes, f) != bytes) {
    fprintf(stderr, "gm2: cannot read %s\n", path);
    free(buf);
    free(words);
    fclose(f);
    return -1;
  }
  fclose(f);
  for (size_t i = 0; i < bytes / 3; i++) {
    words[i] = ((uint32_t)buf[3 * i] << 16) | ((uint32_t)buf[3 * i + 1] << 8) | buf[3 * i + 2];
  }
  free(buf);
  *out = words;
  *out_words = (uint32_t)(bytes / 3);
  return 0;
}

/* Value of a `0x`-prefixed hex load address, or -1 when it is not one. */
static int parse_hex(const char *text, uint32_t *out) {
  uint32_t value = 0;
  if (text[0] != '0' || (text[1] != 'x' && text[1] != 'X') || text[2] == '\0') {
    return -1;
  }
  for (text += 2; *text != '\0'; text++) {
    uint32_t digit;
    if (*text >= '0' && *text <= '9') {
      digit = (uint32_t)(*text - '0');
    } else if (*text >= 'a' && *text <= 'f') {
      digit = (uint32_t)(*text - 'a') + 10u;
    } else if (*text >= 'A' && *text <= 'F') {
      digit = (uint32_t)(*text - 'A') + 10u;
    } else {
      return -1;
    }
    value = value * 16u + digit;
    if (value > GM2_MEM_WORDS) {
      return -1;
    }
  }
  *out = value;
  return 0;
}

static int parse_dec(const char *text, uint32_t *out) {
  uint32_t value = 0;
  if (*text == '\0') {
    return -1;
  }
  for (; *text != '\0'; text++) {
    if (*text < '0' || *text > '9') {
      return -1;
    }
    value = value * 10u + (uint32_t)(*text - '0');
    if (value > 0xFFFFFFu) {
      return -1;
    }
  }
  *out = value;
  return 0;
}

/* Splits `line` at its commas, or -1 when it does not hold exactly five fields. */
static int split_row(char *line, char *field[5]) {
  unsigned n = 1;
  field[0] = line;
  for (char *p = line; *p != '\0'; p++) {
    if (*p != ',') {
      continue;
    }
    if (n == 5u) {
      return -1;
    }
    *p = '\0';
    field[n++] = p + 1;
  }
  return n == 5u ? 0 : -1;
}

/* Loads what `<dir>/manifest.csv` lists: every `code` and `data` row into P at its
 * load address, in manifest order. */
int gm2_load_application(gm2_emu *emu, const char *dir, int verbose) {
  char path[512];
  int n = snprintf(path, sizeof path, "%s/manifest.csv", dir);
  if (n < 0 || (size_t)n >= sizeof path) {
    fprintf(stderr, "gm2: path too long: %s/manifest.csv\n", dir);
    return -1;
  }
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    fprintf(stderr, "gm2: cannot open %s: %s\n", path, strerror(errno));
    return -1;
  }
  char line[GM2_MANIFEST_LINE];
  unsigned rows = 0;
  int rc = 0;
  while (rc == 0 && fgets(line, (int)sizeof line, f) != NULL) {
    rows++;
    size_t len = strlen(line);
    if (len + 1u == sizeof line && line[len - 1u] != '\n') {
      fprintf(stderr, "gm2: %s row %u does not fit in %u bytes\n", path, rows, GM2_MANIFEST_LINE);
      rc = -1;
      break;
    }
    while (len > 0 && (line[len - 1u] == '\n' || line[len - 1u] == '\r')) {
      line[--len] = '\0';
    }
    if (rows == 1u) {
      if (strcmp(line, k_manifest_header) != 0) {
        fprintf(stderr, "gm2: %s header is not '%s'\n", path, k_manifest_header);
        rc = -1;
      }
      continue;
    }
    char *field[5];
    if (split_row(line, field) != 0) {
      fprintf(stderr, "gm2: %s row %u is not five comma-separated fields\n", path, rows);
      rc = -1;
      break;
    }
    if (rows == 2u) {
      /* The load stream runs the bootstrap before the application overwrites it. */
      continue;
    }
    if (strcmp(field[4], "terminator") == 0 || strcmp(field[4], "filler") == 0) {
      continue;
    }
    if (strcmp(field[4], "code") != 0 && strcmp(field[4], "data") != 0) {
      fprintf(stderr, "gm2: %s row %u has unknown kind '%s'\n", path, rows, field[4]);
      rc = -1;
      break;
    }
    uint32_t addr = 0;
    uint32_t words = 0;
    uint32_t bytes = 0;
    if (parse_hex(field[1], &addr) != 0 || parse_dec(field[2], &words) != 0 ||
        parse_dec(field[3], &bytes) != 0 || bytes != 3u * words) {
      fprintf(stderr, "gm2: %s row %u has no 0x-prefixed load address, or bytes != 3 * words\n",
              path, rows);
      rc = -1;
      break;
    }
    char record_path[512];
    n = snprintf(record_path, sizeof record_path, "%s/%s", dir, field[0]);
    if (n < 0 || (size_t)n >= sizeof record_path) {
      fprintf(stderr, "gm2: path too long: %s/%s\n", dir, field[0]);
      rc = -1;
      break;
    }
    uint32_t *data = NULL;
    uint32_t count = 0;
    if (read_u24be(record_path, &data, &count) != 0) {
      rc = -1;
      break;
    }
    if (count != words) {
      fprintf(stderr, "gm2: %s has %u words, manifest says %u\n", field[0], count, words);
      free(data);
      rc = -1;
      break;
    }
    if (addr >= GM2_MEM_WORDS || count > GM2_MEM_WORDS - addr) {
      fprintf(stderr, "gm2: %s does not fit at P:0x%06X\n", field[0], addr);
      free(data);
      rc = -1;
      break;
    }
    gm2_write_run(emu, GM2_P, addr, data, count);
    if (verbose) {
      printf("P:0x%06X  %5u words  %s\n", addr, count, field[0]);
    }
    free(data);
  }
  if (fclose(f) != 0 && rc == 0) {
    fprintf(stderr, "gm2: cannot close %s: %s\n", path, strerror(errno));
    rc = -1;
  }
  return rc;
}

static void copy_words(gm2_emu *emu, enum gm2_space sp, uint32_t dst, uint32_t src,
                       uint32_t count) {
  /* One word at a time, like the DSP loop, so overlaps smear alike. */
  for (uint32_t i = 0; i < count; i++) {
    gm2_write(emu, sp, dst + i, gm2_read(emu, GM2_P, src + i));
  }
}

static void clear_words(gm2_emu *emu, enum gm2_space sp, uint32_t dst, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    gm2_write(emu, sp, dst + i, 0);
  }
}

/* An L: access is one 48-bit word: the high half lands in X and the low half in
 * Y at the same address (measured with move x,l:(r0)+ / move a10,l:(r0)+). */
static void copy_long(gm2_emu *emu, uint32_t dst, uint32_t src, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    gm2_write(emu, GM2_X, dst + i, gm2_read(emu, GM2_P, src + 2 * i));
    gm2_write(emu, GM2_Y, dst + i, gm2_read(emu, GM2_P, src + 2 * i + 1));
  }
}

static void clear_long(gm2_emu *emu, uint32_t dst, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    gm2_write(emu, GM2_X, dst + i, 0);
    gm2_write(emu, GM2_Y, dst + i, 0);
  }
}

/* The six flag words P:0x082AA2 dispatches on; it also has a P-space form that
 * v1.02 never uses, which is reported rather than guessed at. */
static void apply_descriptor(gm2_emu *emu, uint32_t index, uint32_t flags, uint32_t dst,
                             uint32_t src, uint32_t count) {
  switch (flags) {
  case 0x201:
    copy_words(emu, GM2_X, dst, src, count);
    break;
  case 0x301:
    copy_words(emu, GM2_Y, dst, src, count);
    break;
  case 0x202:
    clear_words(emu, GM2_X, dst, count);
    break;
  case 0x302:
    clear_words(emu, GM2_Y, dst, count);
    break;
  case 0x401:
    copy_long(emu, dst, src, count);
    break;
  case 0x402:
    clear_long(emu, dst, count);
    break;
  default:
    fprintf(stderr, "gm2: descriptor %u has unknown flags 0x%03X, skipped\n", index, flags);
    break;
  }
}

int gm2_apply_init_table(gm2_emu *emu) {
  for (uint32_t d = 0; d < GM2_INIT_MAX_DESC; d++) {
    uint32_t flags = gm2_read(emu, GM2_P, GM2_INIT_TABLE + 4 * d);
    if (flags == 0) {
      return (int)d;
    }
    apply_descriptor(emu, d, flags, gm2_read(emu, GM2_P, GM2_INIT_TABLE + 4 * d + 1),
                     gm2_read(emu, GM2_P, GM2_INIT_TABLE + 4 * d + 2),
                     gm2_read(emu, GM2_P, GM2_INIT_TABLE + 4 * d + 3));
  }
  return -1;
}

int gm2_block_levels(const char *dir, unsigned rate, unsigned algorithm, uint32_t ui,
                     uint32_t activation, uint32_t *level_addr, uint32_t words[3]) {
  gm2_emu *emu = gm2_create();
  if (emu == NULL) {
    fprintf(stderr, "gm2: cannot create emulator\n");
    return -1;
  }
  if (gm2_load_application(emu, dir, 0) != 0 || gm2_apply_init_table(emu) < 0) {
    gm2_destroy(emu);
    return -1;
  }
  gm2_reset_regs(emu);
  if (gm2_install_rate(emu, rate) != 0 || gm2_mirror_descriptor(emu, algorithm) < 0) {
    gm2_destroy(emu);
    return -1;
  }
  gm2_write(emu, GM2_X, ui + 1, 1);
  gm2_set_reg(emu, DSP56300_REG_A2, 0);
  gm2_set_reg(emu, DSP56300_REG_A1, algorithm);
  gm2_set_reg(emu, DSP56300_REG_A0, 0);
  gm2_set_reg(emu, DSP56300_REG_R0, ui);
  gm2_set_reg(emu, DSP56300_REG_R7, 0x1000u);
  if (gm2_call(emu, activation, 20000000, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "gm2: block activation P:0x%06X did not return\n", activation);
    gm2_destroy(emu);
    return -1;
  }
  *level_addr = gm2_read(emu, GM2_X, ui + 14);
  for (unsigned i = 0; i < 3; i++) {
    words[i] = gm2_read(emu, GM2_X, ui + 25 + i);
  }
  gm2_destroy(emu);
  return 0;
}

uint32_t gm2_get_reg(const gm2_emu *emu, unsigned reg) {
  Dsp56300State state;
  dsp56300_get_state(emu->dsp, &state);
  return reg < DSP56300_REG_COUNT ? state.registers[reg] : 0;
}

void gm2_set_reg(gm2_emu *emu, unsigned reg, uint32_t value) {
  if (reg >= DSP56300_REG_COUNT) {
    return;
  }
  Dsp56300State state;
  dsp56300_get_state(emu->dsp, &state);
  state.registers[reg] = value;
  dsp56300_set_state(emu->dsp, &state);
}

void gm2_reset_regs(gm2_emu *emu) {
  /* Keeps the host-owned X/Y/P buffers; only the core state is reset. */
  dsp56300_reset(emu->dsp);
}

/* The stub only changes when the entry point does; rewriting P per call would
 * invalidate the JIT block for it every time. */
static void ensure_stub(gm2_emu *emu, uint32_t target) {
  if (emu->stub_valid && emu->stub_target == target) {
    return;
  }
  const uint32_t stub[3] = {GM2_OP_JSR, target, GM2_OP_STOP};
  gm2_write_run(emu, GM2_P, GM2_STUB_ADDR, stub, 3);
  emu->stub_valid = 1;
  emu->stub_target = target;
}

static int run_state(gm2_emu *emu, int32_t budget, uint32_t *cycles) {
  Dsp56300State state;
  dsp56300_get_state(emu->dsp, &state);
  // cppcheck-suppress variableScope -- read before the run, not after
  uint32_t before = state.cycle_count;
  state.power_state = 0;
  state.halt_requested = false;
  dsp56300_set_state(emu->dsp, &state);

  dsp56300_run(emu->dsp, budget);

  dsp56300_get_state(emu->dsp, &state);
  /* STOP ends the run with PC one word past it, so landing on stub+3 is the
   * only evidence that the callee returned instead of running out of budget. */
  if (cycles != NULL) {
    *cycles = state.cycle_count - before;
  }
  return (state.power_state != 0 && state.pc == GM2_STUB_ADDR + 3) ? GM2_CALL_OK
                                                                   : GM2_CALL_NO_RETURN;
}

int gm2_call(gm2_emu *emu, uint32_t target, int32_t budget, uint32_t *cycles) {
  ensure_stub(emu, target);

  Dsp56300State state;
  dsp56300_get_state(emu->dsp, &state);
  state.pc = GM2_STUB_ADDR;
  dsp56300_set_state(emu->dsp, &state);
  return run_state(emu, budget, cycles);
}

int gm2_run_until_stop(gm2_emu *emu, uint32_t pc, int32_t budget, uint32_t *cycles) {
  ensure_stub(emu, pc);
  Dsp56300State state;
  dsp56300_get_state(emu->dsp, &state);
  state.pc = pc;
  dsp56300_set_state(emu->dsp, &state);
  return run_state(emu, budget, cycles);
}
