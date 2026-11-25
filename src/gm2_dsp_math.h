/* DSP56300 fixed-point primitives as the G-Major 2 application executes them.
 *
 * Each function reproduces one measured instruction semantic of the xemu
 * DSP56300 core in the state the firmware task runs in (SR = 0x800300,
 * OMR = 0x004B81): the multiplier does not saturate, the accumulator wraps
 * modulo 2^56, and the move stores limit to the destination width. A 24-bit
 * word is 1.23 fixed point, the accumulator's 1.0 is 2^47.
 */
#ifndef GM2_DSP_MATH_H
#define GM2_DSP_MATH_H

#include <stdint.h>

enum { GM2_W24_MAX = 0x7FFFFF, GM2_W24_MIN = -0x800000 };

#ifndef __cplusplus /* the library is compiled as C; C++ hosts only include this */
// cppcheck-suppress shiftNegativeLHS -- DSP ASR, defined by GCC/Clang
_Static_assert((-1 >> 1) == -1, "gm2_floor_step24 needs an arithmetic right shift");
#endif

/* 24-bit word <-> signed value. */
static inline int32_t gm2_s24(uint32_t word) {
  return (int32_t)((word & 0xFFFFFFu) ^ 0x800000u) - 0x800000;
}

static inline uint32_t gm2_u24(int32_t value) { return (uint32_t)value & 0xFFFFFFu; }

/* Wrap into the signed 56-bit accumulator range. */
static inline int64_t gm2_acc56(int64_t value) {
  return (int64_t)(((uint64_t)value + 0x80000000000000ULL) & 0xFFFFFFFFFFFFFFULL) -
         0x80000000000000LL;
}

/* Floor division by 2^24: the accumulator's A1 field. */
static inline int64_t gm2_floor_step24(int64_t value) {
  // cppcheck-suppress shiftNegativeLHS -- DSP ASR, defined by GCC/Clang
  return value >> 24;
}

/* MPYR/MACR/RND rounding: half to even onto the 24-bit grid. Adding 2^23 rounds
 * half up; only a tie leaves the low 24 bits zero, and clearing bit 24 then
 * turns it to the even neighbour. */
static inline int64_t gm2_round24(int64_t value) {
  int64_t biased = value + 0x800000LL;
  if ((biased & 0xFFFFFFLL) == 0) {
    biased &= ~0x1000000LL;
  }
  return biased & ~0xFFFFFFLL;
}

/* MPY, MPYR, MAC, MACR, ASL and ADDL. */
static inline int64_t gm2_mpy(int32_t s1, int32_t s2) {
  return gm2_acc56(2 * (int64_t)s1 * (int64_t)s2);
}

static inline int64_t gm2_mpyr(int32_t s1, int32_t s2) {
  return gm2_round24(2 * (int64_t)s1 * (int64_t)s2);
}

static inline int64_t gm2_mac(int64_t acc, int32_t s1, int32_t s2) {
  return gm2_acc56(acc + 2 * (int64_t)s1 * (int64_t)s2);
}

static inline int64_t gm2_macr(int64_t acc, int32_t s1, int32_t s2) {
  return gm2_round24(gm2_acc56(acc + 2 * (int64_t)s1 * (int64_t)s2));
}

/* DMACSU and DMACSS: the accumulator moves down a whole 24-bit field before the
 * doubled product is added, so a pair of them multiplies a 48-bit value by a
 * 24-bit word. DMACSU reads its second operand as unsigned, which is how the low
 * half of a two's complement difference gets multiplied. */
static inline int64_t gm2_dmacsu(int64_t acc, int32_t s1, uint32_t s2u) {
  return gm2_acc56(gm2_floor_step24(acc) + 2 * (int64_t)s1 * (int64_t)s2u);
}

static inline int64_t gm2_dmacss(int64_t acc, int32_t s1, int32_t s2) {
  return gm2_acc56(gm2_floor_step24(acc) + 2 * (int64_t)s1 * (int64_t)s2);
}

static inline int64_t gm2_asl(int64_t acc) { return gm2_acc56(acc + acc); }

/* INC/DEC: one 56-bit step, wrapping like any other add. */
static inline int64_t gm2_inc(int64_t acc) { return gm2_acc56(acc + 1); }

static inline int64_t gm2_dec(int64_t acc) { return gm2_acc56(acc - 1); }

static inline int64_t gm2_addl(int64_t source, int64_t destination) {
  return gm2_acc56(source + destination + destination);
}

static inline int64_t gm2_addr(int64_t source, int64_t destination) {
  // cppcheck-suppress shiftNegativeLHS -- DSP ASR, defined by GCC/Clang
  return gm2_acc56(source + (destination >> 1));
}

/* ASL #n and ASR #n shift the whole accumulator, wrapping and sign filling, with
 * no saturation in this SR state. The left shift is a multiply because shifting a
 * negative value is undefined. */
static inline int64_t gm2_asl_n(int64_t acc, unsigned n) {
  return gm2_acc56((int64_t)(((uint64_t)acc * ((uint64_t)1 << n)) & 0xFFFFFFFFFFFFFFULL));
}

static inline int64_t gm2_asr_n(int64_t acc, unsigned n) {
  // cppcheck-suppress shiftNegativeLHS -- DSP ASR, defined by GCC/Clang
  return acc >> n;
}

/* LSR A: only the A1 field shifts, logically; A2 and A0 keep their bits. */
static inline int64_t gm2_lsr_a1(int64_t acc) {
  uint64_t u = (uint64_t)acc & 0xFFFFFFFFFFFFFFULL;
  uint64_t a1 = (u >> 24) & 0xFFFFFFu;
  return gm2_acc56((int64_t)((u & 0xFF000000FFFFFFULL) | ((a1 >> 1) << 24)));
}

/* AND #imm,A: the immediate is masked into the A1 field only. */
static inline int64_t gm2_and_a1(int64_t acc, uint32_t mask) {
  uint64_t u = (uint64_t)acc & 0xFFFFFFFFFFFFFFULL;
  uint64_t a1 = ((u >> 24) & mask & 0xFFFFFFu) << 24;
  return gm2_acc56((int64_t)((u & 0xFF000000FFFFFFULL) | a1));
}

/* ABS and NEG wrap the 56-bit accumulator; the most negative value stays put. */
static inline int64_t gm2_neg56(int64_t acc) { return gm2_acc56(-acc); }

static inline int64_t gm2_abs56(int64_t acc) { return acc < 0 ? gm2_neg56(acc) : acc; }

/* ES: the extension bits are in use, which the DSP sets from 2^47 up and below
 * -2^47; anything inside that range fits a 24-bit word at the A1 position. */
static inline int gm2_es(int64_t acc) { return acc > 0x7FFFFFFFFFFFLL || acc < -0x800000000000LL; }

/* The accumulator value a 24-bit operand is read as: sign extended into A1. */
static inline int64_t gm2_word_acc(int32_t word) { return (int64_t)word * 0x1000000; }

/* ADD and SUB with a 24-bit register or memory operand. */
static inline int64_t gm2_add24(int64_t acc, int32_t word) {
  return gm2_acc56(acc + gm2_word_acc(word));
}

static inline int64_t gm2_sub24(int64_t acc, int32_t word) {
  return gm2_acc56(acc - gm2_word_acc(word));
}

/* The A1 field as the 24-bit word a register move reads out of it. */
static inline int32_t gm2_a1(int64_t acc) { return (int32_t)(((uint64_t)acc >> 24) & 0xFFFFFFu); }

/* `move a0,a1`: the middle field takes the low field's bits, A0 keeps them. */
static inline int64_t gm2_a0_to_a1(int64_t acc) {
  uint64_t u = (uint64_t)acc & 0xFFFFFFFFFFFFFFULL;
  uint64_t a0 = u & 0xFFFFFFu;
  return gm2_acc56((int64_t)((u & 0xFF000000000000ULL) | (a0 << 24) | a0));
}

/* MPY S,#n,D with an immediate power of two: S * 2^-n at the A1 position, exactly
 * as the multiplier leaves it, without rounding or limiting. */
static inline int64_t gm2_mpy_pow2(int32_t s, unsigned n) {
  return (int64_t)s * (int64_t)(1u << (24u - n));
}

/* NORMF: shift the accumulator by the count, left when it is negative. Only the
 * low six bits of the magnitude count, so 0 and every multiple of 64 leave the
 * accumulator alone. */
static inline int64_t gm2_normf(int64_t acc, int32_t count) {
  uint32_t magnitude = (uint32_t)(count < 0 ? -count : count) & 0x3Fu;
  if (magnitude == 0) {
    return acc;
  }
  return count < 0 ? gm2_asl_n(acc, magnitude) : gm2_asr_n(acc, magnitude);
}

/* CLB: nine minus the number of leading bits that match the accumulator's sign,
 * which is one less than the normalizing shift with the eight extension bits
 * counted; a zero accumulator gives 0. */
static inline int32_t gm2_clb(int64_t acc) {
  uint64_t u = (uint64_t)acc & 0xFFFFFFFFFFFFFFULL;
  if (u == 0) {
    return 0;
  }
  if ((u >> 55) != 0) {
    u = ~u & 0xFFFFFFFFFFFFFFULL;
  }
  int top = u == 0 ? -1 : 63 - __builtin_clzll(u);
  return top - 46;
}

/* P:0x0832CB: the accessor's clamp of a raw parameter word into its descriptor
 * window, comparing the 24-bit words as it reads them: a value at or below the
 * floor gives the floor, one above the ceiling the ceiling. */
static inline int gm2_clamp_raw(int raw, int lo, int hi) {
  int32_t word = gm2_s24((uint32_t)raw);
  if (word <= lo) {
    return lo;
  }
  return word > hi ? hi : (int)word;
}

/* CMPM: compares magnitudes, so the LT condition after `cmpm word,acc` means the
 * accumulator is the smaller one. */
static inline int gm2_cmpm_lt(int64_t acc, int32_t word) {
  return gm2_abs56(acc) < gm2_abs56(gm2_word_acc(word));
}

/* The magnitude of a 56-bit accumulator, which the most negative value needs a
 * width more than int64 holds. */
static inline uint64_t gm2_mag56(int64_t acc) {
  uint64_t u = (uint64_t)acc & 0xFFFFFFFFFFFFFFULL;
  return (u & 0x80000000000000ULL) != 0 ? 0x100000000000000ULL - u : u;
}

/* MAXM: the destination takes the operand with the larger magnitude and keeps its
 * own on a tie. */
static inline int64_t gm2_maxm(int64_t a, int64_t b) { return gm2_mag56(a) > gm2_mag56(b) ? a : b; }

/* The 24-bit limit on a word that already sits at the A1 position. */
static inline int32_t gm2_limit24(int32_t word) {
  if (word > GM2_W24_MAX) {
    return GM2_W24_MAX;
  }
  return word < GM2_W24_MIN ? GM2_W24_MIN : word;
}

/* Accumulator read keeping only the 24 most significant bits, limited to that
 * width. This is the value `move a,x0` and `move a,x:ea` produce, and it is also
 * the high half of the 48-bit value `move b,l:(r1)+` stores: the 48-bit limit
 * and the 24-bit limit rail at the same word. */
static inline int32_t gm2_store24(int64_t acc) {
  int64_t word = gm2_floor_step24(acc);
  if (word > GM2_W24_MAX) {
    return GM2_W24_MAX;
  }
  return word < GM2_W24_MIN ? GM2_W24_MIN : (int32_t)word;
}

/* P:0x0003AF, which the wah routine also inlines: a parabolic soft clip. Above the
 * knee word the magnitude bends towards the rail word and holds it from the end
 * word on; the result is shifted left by `shift`. `words` are {rail, knee, end,
 * curve shift}. */
static inline int64_t gm2_soft_clip(int64_t acc, const int32_t words[4], unsigned shift) {
  int64_t a = gm2_abs56(acc);
  if (a > gm2_word_acc(words[1])) {
    a = gm2_sub24(a, words[2]);
    if (a >= 0) {
      a = 0;
    }
    int32_t over = gm2_store24(a);
    a = gm2_acc56(-gm2_mpy(over, over));
    a = gm2_add24(gm2_asl_n(a, (unsigned)words[3] & 0x3Fu), words[0]);
  }
  if (acc < 0) {
    a = gm2_neg56(a);
  }
  return gm2_asl_n(a, shift);
}

/* `move b,l:(r1)+`, limited to 48 bits. */
static inline int64_t gm2_store48(int64_t acc) {
  if (acc > 0x7FFFFFFFFFFFLL) {
    return 0x7FFFFFFFFFFFLL;
  }
  return acc < -0x800000000000LL ? -0x800000000000LL : acc;
}

/* `move b1,y0; move b2,y1` after loading a 24-bit word into B: sign-extends
 * the 24-bit word into a 48-bit integer in bits 47..0. */
static inline int64_t gm2_sext48(int32_t word) {
  int64_t v = gm2_s24((uint32_t)word);
  return v < 0 ? (v & 0xFFFFFFLL) | (-1LL * (1LL << 24)) : (v & 0xFFFFFFLL);
}

/* `move a0,a`: the low 24-bit field of a 48-bit or 56-bit accumulator, sign
 * extended as a 24-bit word. */
static inline int32_t gm2_lo24(int64_t acc) {
  return gm2_s24((uint32_t)((uint64_t)acc & 0xFFFFFFu));
}

/* `mpyuu +y0,x0,b; move b0,a0; dmacsu +x1,y0,b; macsu +y1,x0,b; move b0,a1;
 * dmacss +y1,x1,b; move b0,a2; asr a`: 48-bit by 48-bit integer multiply
 * returning the 56-bit accumulator. */
static inline int64_t gm2_mul48(int64_t x, int64_t y) {
  uint32_t x0 = (uint32_t)x & 0xFFFFFFu;
  int32_t x1 = gm2_s24((uint32_t)((uint64_t)x >> 24));
  uint32_t y0 = (uint32_t)y & 0xFFFFFFu;
  int32_t y1 = gm2_s24((uint32_t)((uint64_t)y >> 24));
  int64_t b = gm2_acc56(2LL * (int64_t)y0 * (int64_t)x0);
  uint32_t a0 = (uint32_t)b & 0xFFFFFFu;
  b = gm2_dmacsu(b, x1, y0);
  b = gm2_acc56(b + 2LL * (int64_t)y1 * (int64_t)x0);
  uint32_t a1 = (uint32_t)b & 0xFFFFFFu;
  b = gm2_dmacss(b, y1, x1);
  uint32_t a2 = (uint32_t)b & 0xFFu;
  int64_t a = gm2_acc56((int64_t)(((uint64_t)a2 << 48) | ((uint64_t)a1 << 24) | a0));
  return gm2_asr_n(a, 1);
}

/* P:0x09770E: 48-bit signed integer division a / b, returning 0 when |a| < |b|. */
static inline int64_t gm2_div48(int64_t a, int64_t b) {
  int64_t ma = a < 0 ? -a : a;
  int64_t mb = b < 0 ? -b : b;
  if (ma < mb || mb == 0) {
    return 0;
  }
  return a / b;
}

#endif /* GM2_DSP_MATH_H */
