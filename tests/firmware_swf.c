/* The firmware's software-float routines, called on the emulator: every routine
 * the native port exposes, over boundary operands, a fixed-seed random set and
 * the paired second and third operand lists the effect chains use.
 */
#include "firmware_common.h"
#include "gm2_dsp_math.h"
#include "gm2_emu.h"
#include "gm2_fixture.h"
#include "gm2_record.h"
#include "gm2_swf.h"

#include <dsp56300.h>

#include <stdio.h>
#include <string.h>

#define XSTACK 0x1000u
#define CALL_BUDGET 400000
#define RECORDS_MAX 40000u

#define P_ADD 0x000B65u
#define P_SUB 0x000B5Fu
#define P_MUL 0x000B8Bu
#define P_DIV 0x000B9Bu
#define P_NEG 0x000BBAu
#define P_CMP 0x000C39u
#define P_MAC 0x000B37u
#define P_MSUB 0x000B4Au
#define P_INT2F 0x000BFDu
#define P_F2INT 0x000BC6u
#define P_ABS 0x097026u
#define P_EXP 0x096FAAu
#define P_LN 0x097052u
#define P_POW 0x0970F1u
#define P_SQRT 0x0971DAu
#define P_SINH 0x097147u
#define P_ATAN 0x096F90u
#define P_TAN 0x09726Du
#define P_SIN 0x097140u
#define P_COS 0x096F9Fu
#define P_HZ20 0x08F0DFu
#define P_HZ80 0x08F0F8u
#define P_DB 0x082E01u
#define P_DB_TO_LIN 0x082DF1u
#define P_TO_INT48 0x000BE1u
#define P_FROM_INT48 0x000C07u

#define INPUTS_MAX 512u
#define SECOND_MAX 32u
#define THIRD_MAX 16u
#define RANDOM_INPUTS 96u

static const gm2_swf k_zero = {0, 0};
static gm2_emu *emu;
static gm2_fixture_writer writer;
static unsigned records;

static int prepare(void) {
  emu = gm2_create();
  if (emu == NULL) {
    fprintf(stderr, "swf: cannot create the emulator\n");
    return -1;
  }
  if (gm2_load_application(emu, GM2_FIRMWARE_DATA_DIR, 0) != 0 || gm2_apply_init_table(emu) < 0) {
    fprintf(stderr, "swf: cannot load the application image\n");
    return -1;
  }
  return 0;
}

/* The first operand sits in the accumulator as A1 with the sign extended into A2
 * and the exponent in B1, the second in X1/X0, the third in Y1/Y0. 0, or -1 after
 * a message when the routine never returned. */
static int firmware(unsigned target, gm2_swf a, gm2_swf b, gm2_swf c, gm2_swf *out) {
  gm2_reset_regs(emu);
  gm2_set_reg(emu, DSP56300_REG_R7, XSTACK);
  gm2_set_reg(emu, DSP56300_REG_A2, a.m < 0 ? 0xFFu : 0u);
  gm2_set_reg(emu, DSP56300_REG_A1, gm2_word24(a.m));
  gm2_set_reg(emu, DSP56300_REG_A0, 0);
  gm2_set_reg(emu, DSP56300_REG_B2, 0);
  gm2_set_reg(emu, DSP56300_REG_B1, gm2_word24(a.e));
  gm2_set_reg(emu, DSP56300_REG_B0, 0);
  gm2_set_reg(emu, DSP56300_REG_X1, gm2_word24(b.m));
  gm2_set_reg(emu, DSP56300_REG_X0, gm2_word24(b.e));
  gm2_set_reg(emu, DSP56300_REG_Y1, gm2_word24(c.m));
  gm2_set_reg(emu, DSP56300_REG_Y0, gm2_word24(c.e));
  if (gm2_call(emu, target, CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "swf: P:0x%06X did not return for {%06X,%02X}\n", target, gm2_word24(a.m),
            (unsigned)a.e & 0xFFu);
    return -1;
  }
  out->m = gm2_sign24(gm2_get_reg(emu, DSP56300_REG_A1));
  out->e = (int)(gm2_get_reg(emu, DSP56300_REG_B1) & 0xFFu);
  return 0;
}

/* P:0x000C39 leaves only the condition codes: N says the first operand was the
 * smaller one, Z that they compared equal. */
static int firmware_cmp(gm2_swf a, gm2_swf b) {
  gm2_swf ignored;
  if (firmware(P_CMP, a, b, k_zero, &ignored) != 0) {
    return -2;
  }
  uint32_t sr = gm2_get_reg(emu, DSP56300_REG_SR);
  if ((sr & 0x4u) != 0u) {
    return 0;
  }
  return (sr & 0x8u) != 0u ? -1 : 1;
}

static int record(unsigned op, gm2_swf a, gm2_swf b, gm2_swf c, gm2_swf result) {
  if (records >= RECORDS_MAX) {
    fprintf(stderr, "swf: more than %u operations\n", RECORDS_MAX);
    return -1;
  }
  gm2_fixture_swf entry;
  memset(&entry, 0, sizeof entry);
  entry.op = op;
  entry.a_m = a.m;
  entry.a_e = a.e;
  entry.b_m = b.m;
  entry.b_e = b.e;
  entry.c_m = c.m;
  entry.c_e = c.e;
  entry.r_m = result.m;
  entry.r_e = result.e;
  unsigned char bytes[GM2_FIX_SWF_SIZE];
  gm2_fixture_put_swf(bytes, &entry);
  records++;
  return gm2_fixture_writer_add(&writer, GM2_FIX_SWF, sizeof bytes, bytes);
}

/* One firmware answer over the three-operand convention. */
static int record_call(unsigned op, unsigned target, gm2_swf a, gm2_swf b, gm2_swf c) {
  gm2_swf answer;
  if (firmware(target, a, b, c, &answer) != 0) {
    return -1;
  }
  return record(op, a, b, c, answer);
}

/* --- operands ------------------------------------------------------------ */

/* Rounds onto the firmware's 24-bit grid: only host libm picks operands, every
 * answer recorded is the firmware's own. */
static gm2_swf canonical(gm2_swf value) {
  if (value.m == 0) {
    return k_zero;
  }
  while (value.m >= 0x800000) {
    value.m /= 2;
    value.e += 1;
  }
  while (value.m > -0x400000 && value.m < 0x400000) {
    value.m *= 2;
    value.e -= 1;
  }
  value.e &= 0xFF;
  return value;
}

static gm2_swf negate(gm2_swf value) {
  if (value.m == 0) {
    return k_zero;
  }
  gm2_swf result = {-value.m, value.e};
  if (result.m == 0x800000) {
    result.m = 0x400000;
    result.e += 1;
  }
  return canonical(result);
}

static unsigned boundary_inputs(gm2_swf *out, unsigned cap) {
  static const struct {
    int32_t m;
    int e;
  } k_edges[] = {
      {0, 0},
      {0, 0x7F},
      {0, 0x80},
      {0, 0xFF},
      {0x400000, 0x80},
      {-0x400000, 0x80},
      {0x400000, 0x7F},
      {0x400000, 0x81},
      {0x7FFFFF, 0x80},
      {-0x800000, 0x80},
      {0x7FFFFF, 0xFF},
      {-0x800000, 0xFF},
      {0x400000, 0x01},
      {0x7FFFFF, 0x01},
      {0x400000, 0xFE},
      {0x7FFFFF, 0xFE},
      {0x400001, 0x80},
      {0x7FFFFE, 0x80},
      {0x5C551E, 0x80},
      {0x5A827A, 0x7F},
      {0x5A827A, 0x80},
      {0x6A09E6, 0x7E},
      {0x4D413D, 0x81},
      {0x6487EE, 0x80},
      {0x6487ED, 0x7F},
      {0x4A0000, 0x85},
      {0x400000, 0x85},
      {0x400000, 0x86},
      {0x590000, 0x86},
      {-0x590000, 0x86},
      {0x400000, 0x9E},
      {0x7FFFFF, 0x96},
      {-0x800000, 0x96},
      {0x400000, 0x96},
      {0x400000, 0x69},
      {0x400000, 0x6A},
      {0x517CC2, 0x80},
      {0x45ED3E, 0x7F},
      {0x415E17, 0x7E},
  };
  unsigned n = 0;
  for (unsigned i = 0; i < sizeof k_edges / sizeof k_edges[0] && n + 1u < cap; i++) {
    gm2_swf value = canonical((gm2_swf){k_edges[i].m, k_edges[i].e});
    out[n++] = value;
    out[n++] = negate(value);
  }
  for (int e = 0x7C; e <= 0x8A && n + 2u < cap; e++) {
    out[n++] = canonical((gm2_swf){0x400000, e});
    out[n++] = canonical((gm2_swf){0x7FFFFF, e});
    out[n++] = canonical((gm2_swf){-0x800000, e});
  }
  return n;
}

static uint32_t rng_state = 0x2545F491u;

static uint32_t rng(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

static gm2_swf random_swf(int exp_lo, int exp_hi) {
  gm2_swf value;
  uint32_t bits = rng();
  value.m = (int32_t)(0x400000u + (bits & 0x3FFFFFu));
  if ((bits & 0x40000000u) != 0u) {
    value.m = -value.m;
  }
  value.e = exp_lo + (int)((bits >> 8) % (uint32_t)(exp_hi - exp_lo + 1));
  if ((rng() & 15u) == 0u) {
    value = k_zero;
  }
  return value;
}

/* tan's argument reduction goes through P:0x0970D3, which leaves values of 2^23
 * and up whole: past that the quadrant, not the routine, decides the answer. */
static int tan_range(gm2_swf value) { return value.e < 0x8D; }

/* --- sweeps -------------------------------------------------------------- */

static int sweep_unary(unsigned op, unsigned target, const gm2_swf *in, unsigned count) {
  for (unsigned i = 0; i < count; i++) {
    if (record_call(op, target, in[i], k_zero, k_zero) != 0) {
      return -1;
    }
  }
  return 0;
}

static unsigned pair_stride(unsigned count) { return count / 4u + 1u; }

static int sweep_binary(unsigned op, unsigned target, const gm2_swf *in, unsigned count,
                        const gm2_swf *second, unsigned second_count, int swapped) {
  unsigned stride = pair_stride(second_count);
  for (unsigned i = 0; i < count; i++) {
    for (unsigned j = 0; j < second_count; j += stride) {
      gm2_swf a = swapped != 0 ? second[j] : in[i];
      gm2_swf b = swapped != 0 ? in[i] : second[j];
      if (record_call(op, target, a, b, k_zero) != 0) {
        return -1;
      }
    }
  }
  return 0;
}

static int sweep_ternary(unsigned op, unsigned target, const gm2_swf *in, unsigned count,
                         const gm2_swf *third, unsigned third_count) {
  static const gm2_swf k_y[] = {{0x400000, 0x80}, {0x7FFFFF, 0x7F}, {-0x400000, 0x80}};
  unsigned stride = pair_stride(third_count);
  for (unsigned i = 0; i < count; i++) {
    for (unsigned j = 0; j < third_count; j += stride) {
      gm2_swf y = k_y[(i + j) % (sizeof k_y / sizeof k_y[0])];
      if (record_call(op, target, in[i], third[j], y) != 0) {
        return -1;
      }
    }
  }
  return 0;
}

static int sweep_compare(const gm2_swf *in, unsigned count) {
  for (unsigned i = 0; i < count; i++) {
    for (unsigned j = 0; j < count; j++) {
      int answer = firmware_cmp(in[i], in[j]);
      if (answer == -2 || record(GM2_SWF_CMP, in[i], in[j], k_zero, (gm2_swf){answer, 0}) != 0) {
        return -1;
      }
    }
  }
  return 0;
}

/* P:0x000BC6 leaves a 24-bit integer in A1; B1 keeps a scratch exponent, so only
 * the mantissa is recorded. P:0x000BFD takes the integer in A1. */
static int sweep_integer_conversions(void) {
  for (int32_t n = -32; n <= 32; n += 2) {
    if (record_call(GM2_SWF_FROM_INT, P_INT2F, (gm2_swf){n, 0}, k_zero, k_zero) != 0) {
      return -1;
    }
  }
  for (unsigned i = 0; i < 32u; i++) {
    int32_t n = gm2_sign24(rng() & 0xFFFFFFu);
    if (record_call(GM2_SWF_FROM_INT, P_INT2F, (gm2_swf){n, 0}, k_zero, k_zero) != 0) {
      return -1;
    }
  }
  for (unsigned i = 0; i < 65u; i++) {
    gm2_swf value = random_swf(0x60, 0xA0);
    gm2_swf answer;
    if (firmware(P_F2INT, value, k_zero, k_zero, &answer) != 0 ||
        record(GM2_SWF_TO_INT, value, k_zero, k_zero, (gm2_swf){answer.m, 0}) != 0) {
      return -1;
    }
  }
  return 0;
}

/* P:0x000BE1 leaves the 48-bit integer in A1:A0 with A2 the sign, P:0x000C07
 * takes that integer back into a value. */
static int64_t accumulator(void) {
  const uint32_t a2 = gm2_get_reg(emu, DSP56300_REG_A2);
  const uint32_t a1 = gm2_get_reg(emu, DSP56300_REG_A1);
  const uint32_t a0 = gm2_get_reg(emu, DSP56300_REG_A0);
  return gm2_acc56((int64_t)(((uint64_t)a2 << 48) | ((uint64_t)a1 << 24) | a0));
}

static int record_to_int48(gm2_swf value) {
  gm2_reset_regs(emu);
  gm2_set_reg(emu, DSP56300_REG_R7, XSTACK);
  gm2_set_reg(emu, DSP56300_REG_A2, value.m < 0 ? 0xFFu : 0u);
  gm2_set_reg(emu, DSP56300_REG_A1, gm2_word24(value.m));
  gm2_set_reg(emu, DSP56300_REG_A0, 0);
  gm2_set_reg(emu, DSP56300_REG_B2, 0);
  gm2_set_reg(emu, DSP56300_REG_B1, gm2_word24(value.e));
  gm2_set_reg(emu, DSP56300_REG_B0, 0);
  if (gm2_call(emu, P_TO_INT48, CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "swf: P:0x%06X did not return\n", P_TO_INT48);
    return -1;
  }
  int64_t integer = accumulator();
  if (integer > 0x7FFFFFFFFFFFll || integer < -0x800000000000ll) {
    fprintf(stderr, "swf: P:0x000BE1 returned %lld outside the 48-bit field\n", (long long)integer);
    return -1;
  }
  int32_t low = 0;
  int32_t high = 0;
  gm2_fixture_split48(integer, &low, &high);
  return record(GM2_SWF_TO_INT48, value, k_zero, k_zero, (gm2_swf){low, high});
}

static int record_from_int48(int64_t value) {
  int32_t low = 0;
  int32_t high = 0;
  gm2_fixture_split48(value, &low, &high);
  gm2_reset_regs(emu);
  gm2_set_reg(emu, DSP56300_REG_R7, XSTACK);
  gm2_set_reg(emu, DSP56300_REG_A2, (uint32_t)((uint64_t)value >> 48) & 0xFFu);
  gm2_set_reg(emu, DSP56300_REG_A1, (uint32_t)((uint64_t)value >> 24) & 0xFFFFFFu);
  gm2_set_reg(emu, DSP56300_REG_A0, (uint32_t)(uint64_t)value & 0xFFFFFFu);
  gm2_set_reg(emu, DSP56300_REG_B2, 0);
  gm2_set_reg(emu, DSP56300_REG_B1, 0);
  gm2_set_reg(emu, DSP56300_REG_B0, 0);
  if (gm2_call(emu, P_FROM_INT48, CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "swf: P:0x%06X did not return\n", P_FROM_INT48);
    return -1;
  }
  gm2_swf answer;
  answer.m = gm2_sign24(gm2_get_reg(emu, DSP56300_REG_A1));
  answer.e = (int)(gm2_get_reg(emu, DSP56300_REG_B1) & 0xFFu);
  return record(GM2_SWF_FROM_INT48, (gm2_swf){low, high}, k_zero, k_zero, answer);
}

static int sweep_int48(const gm2_swf *in, unsigned count) {
  static const int64_t k_integers[] = {0,
                                       1,
                                       -1,
                                       2,
                                       3,
                                       23,
                                       200,
                                       500,
                                       -500,
                                       0x7FFFFF,
                                       -0x800000,
                                       0xFFFFFF,
                                       -0x1000000,
                                       0x1000000,
                                       0x800000,
                                       0xC00000,
                                       0x400000,
                                       0x400001,
                                       0x400003,
                                       0x7FFFFFFFFFll,
                                       -0x8000000000ll,
                                       0x7FFFFFFFFFFFll,
                                       -0x800000000000ll,
                                       1ll << 46,
                                       -(1ll << 46),
                                       0x7FFFFF000000ll,
                                       -0x7FFFFF000001ll};
  for (unsigned i = 0; i < count; i++) {
    if (record_to_int48(in[i]) != 0) {
      return -1;
    }
  }
  for (unsigned i = 0; i < sizeof k_integers / sizeof k_integers[0]; i++) {
    if (record_from_int48(k_integers[i]) != 0) {
      return -1;
    }
  }
  for (unsigned i = 0; i < 24u; i++) {
    int64_t value =
        (int64_t)gm2_sign24(rng() & 0xFFFFFFu) * 0x1000000ll + (int64_t)(rng() & 0xFFFFFFu);
    if (record_from_int48(value) != 0) {
      return -1;
    }
  }
  return 0;
}

/* P:0x08F0DF, P:0x08F0F8, P:0x082E01 and P:0x082DF1 take a whole number in A1
 * and answer in A1; P:0x082DF1 only as a 1.23 mantissa. */
static int sweep_raw(unsigned op, unsigned target, int first, int last) {
  for (int raw = first; raw <= last; raw++) {
    gm2_swf answer;
    if (firmware(target, (gm2_swf){raw, 0}, k_zero, k_zero, &answer) != 0) {
      return -1;
    }
    gm2_swf result = op == GM2_SWF_DB_TO_LIN ? (gm2_swf){answer.m, 0} : answer;
    if (record(op, (gm2_swf){raw, 0}, k_zero, k_zero, result) != 0) {
      return -1;
    }
  }
  return 0;
}

int gm2_record_swf(const char *path) {
  static gm2_swf inputs[INPUTS_MAX];
  static gm2_swf second[SECOND_MAX];
  static gm2_swf third[THIRD_MAX];
  static const gm2_swf k_second[] = {
      {0, 0},           {0x400000, 0x80}, {0x400000, 0x7F}, {0x400000, 0x81},
      {0x400000, 0x82}, {0x500000, 0x80}, {0x480000, 0x81}, {-0x400000, 0x80},
      {0x7FFFFF, 0x80}, {0x400000, 0x7E}, {0x400000, 0x83}, {0x600000, 0x81},
      {0x555555, 0x7F}, {0x4A0000, 0x85}, {0x6487ED, 0x7F}, {0x400000, 0xBF},
  };
  if (gm2_fixture_writer_open(&writer, path) != 0 || prepare() != 0) {
    gm2_fixture_writer_abort(&writer);
    gm2_destroy(emu);
    return -1;
  }
  records = 0;

  unsigned count = boundary_inputs(inputs, INPUTS_MAX);
  for (unsigned i = 0; i < RANDOM_INPUTS && count < INPUTS_MAX; i++) {
    inputs[count++] = random_swf(1, 254);
  }
  unsigned second_count = 0;
  for (unsigned i = 0; i < sizeof k_second / sizeof k_second[0]; i++) {
    second[second_count++] = canonical(k_second[i]);
  }
  for (unsigned i = 0; i < 8u; i++) {
    second[second_count++] = random_swf(0x60, 0xA0);
  }
  unsigned third_count = 0;
  for (unsigned i = 0; i < 8u; i++) {
    third[third_count++] = canonical(k_second[i]);
  }
  for (unsigned i = 0; i < 6u; i++) {
    third[third_count++] = random_swf(0x70, 0x90);
  }

  int status = 0;
  status |= sweep_unary(GM2_SWF_EXP, P_EXP, inputs, count);
  status |= sweep_unary(GM2_SWF_LN, P_LN, inputs, count);
  status |= sweep_unary(GM2_SWF_SQRT, P_SQRT, inputs, count);
  status |= sweep_unary(GM2_SWF_SINH, P_SINH, inputs, count);
  status |= sweep_unary(GM2_SWF_SIN, P_SIN, inputs, count);
  status |= sweep_unary(GM2_SWF_COS, P_COS, inputs, count);
  status |= sweep_unary(GM2_SWF_ABS, P_ABS, inputs, count);
  status |= sweep_unary(GM2_SWF_NEG, P_NEG, inputs, count);
  status |= sweep_unary(GM2_SWF_ATAN, P_ATAN, inputs, count);
  for (unsigned i = 0; i < count && status == 0; i++) {
    if (tan_range(inputs[i]) && record_call(GM2_SWF_TAN, P_TAN, inputs[i], k_zero, k_zero) != 0) {
      status = -1;
    }
  }
  status |= sweep_binary(GM2_SWF_ADD, P_ADD, inputs, count, second, second_count, 0);
  status |= sweep_binary(GM2_SWF_SUB, P_SUB, inputs, count, second, second_count, 1);
  status |= sweep_binary(GM2_SWF_MUL, P_MUL, inputs, count, second, second_count, 0);
  status |= sweep_binary(GM2_SWF_DIV, P_DIV, inputs, count, second, second_count, 0);
  status |= sweep_binary(GM2_SWF_POW, P_POW, inputs, count, second, second_count, 0);
  status |= sweep_ternary(GM2_SWF_MAC, P_MAC, inputs, count, third, third_count);
  status |= sweep_ternary(GM2_SWF_MSUB, P_MSUB, inputs, count, third, third_count);
  status |= sweep_compare(inputs, 24u);
  status |= sweep_integer_conversions();
  status |= sweep_int48(inputs, count);
  status |= sweep_raw(GM2_SWF_HZ20, P_HZ20, -128, 300);
  status |= sweep_raw(GM2_SWF_HZ80, P_HZ80, -256, 300);
  status |= sweep_raw(GM2_SWF_DB, P_DB, -120, 20);
  status |= sweep_raw(GM2_SWF_DB_TO_LIN, P_DB_TO_LIN, -120, 20);
  gm2_destroy(emu);
  emu = NULL;

  if (status != 0) {
    gm2_fixture_writer_abort(&writer);
    return -1;
  }
  if (gm2_fixture_writer_close(&writer) != 0) {
    fprintf(stderr, "swf: cannot write %s\n", path);
    return -1;
  }
  printf("%-15s recorded %u operations to %s\n", "swf", records, path);
  return 0;
}
