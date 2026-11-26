/* Ports of the firmware's software-float primitives and transcendental helpers.
 *
 * The application image implements its own float in the ancestor of every DSP
 * fixed-point library: a signed 24-bit mantissa in A1 with an 8-bit exponent in
 * B1, value = m * 2^(e - 150), normalised so that |m| is in [0x400000,
 * 0x800000), zero being m == 0. P:0x000B65 add, 0x000B5F subtract, 0x000B8B
 * multiply, 0x000B9B divide, 0x000BBA negate, 0x000BFD/0x000BC6 integer
 * conversions and P:0x000C39 compare build on a shared tail: CLB/NORMF
 * normalise the 56-bit accumulator, RND rounds half to even onto the 24-bit
 * grid, NORM folds the shift count back into the exponent, and P:0x0BBD rails
 * an exponent that leaves its 8 bits.
 *
 * Above that sit the helpers the Gate/EQ chain (P:0x095C3C) calls: P:0x096FAA
 * exp, P:0x097052 ln, P:0x0970F1 pow, P:0x0971DA sqrt,
 * P:0x097147 sinh, P:0x096F90 atan and P:0x09726D tan, plus P:0x097026 |x|,
 * P:0x0970D3 modf, P:0x0973C9 ldexp and the three-operand MAC forms
 * P:0x000B37/P:0x000B4A.
 *
 * Every routine here is a transcription of the instructions at that address,
 * not a re-derivation from the mathematics: the polynomial constants, the order
 * of the multiply and add steps, the intermediate 24-bit roundings and the
 * range branches are the firmware's.
 */
#include "gm2_swf.h"

#include "gm2_dsp_math.h"

#include <stdint.h>

#define SWF_POW24 0x1000000LL /* 2^24, one 24-bit mantissa step */
#define SWF_ONE_E 0x80        /* exponent of 1.0 in an E-equal division */

/* The mantissa field is signed, and the disassembly's 24-bit words sign-extend
 * the way the accumulator's A2 does: 0xA70000 is -0x590000. */

/* --- accumulator plumbing ------------------------------------------------ */

static int swf_norm_ok(int32_t m) {
  return (m >= 0x400000 && m <= GM2_W24_MAX) || (m <= -0x400001 && m >= GM2_W24_MIN);
}

/* The mantissa field: bits 47..24 of the accumulator, sign-extended. */
static int32_t swf_acc_mantissa(int64_t acc) { return gm2_s24((uint32_t)((acc >> 24) & 0xFFFFFF)); }

/* NORMF: shift left until the mantissa field is normalised, lowering the
 * exponent by the same count. */
static int64_t swf_normf(int64_t acc, int *e) {
  for (;;) {
    int32_t m = swf_acc_mantissa(acc);
    if (swf_norm_ok(m) || m == GM2_W24_MIN) {
      return acc;
    }
    acc = gm2_acc56(acc * 2);
    *e -= 1;
  }
}

/* RND: round half to even onto the 24-bit grid; the result's low 24 bits are
 * cleared. */
static int64_t swf_rnd(int64_t acc) {
  int64_t step = acc / SWF_POW24;
  if (acc % SWF_POW24 < 0) {
    step -= 1;
  }
  int64_t rest = acc - step * SWF_POW24;
  if (rest > SWF_POW24 / 2 || (rest == SWF_POW24 / 2 && (step & 1) != 0)) {
    step += 1;
  }
  return step * SWF_POW24;
}

/* An accumulator past the 24-bit field is halved with the exponent following,
 * the way CLB/NORMF walk it back down. The one exception is a negative sum one
 * LSB past -1.0: RND limits that to -1.0 at the exponent it already has
 * (P:0x0B93), which is why -0x800001 at 0x80 comes back as 0x800000, not as
 * -0x400001 at 0x81. */
static int64_t swf_overshoot(int64_t acc, int *e) {
  const int64_t limit = (int64_t)1 << 47;
  if (acc == -limit - ((int64_t)1 << 24)) {
    return -limit;
  }
  while (acc > limit - 1 || acc < -limit) {
    // cppcheck-suppress shiftNegativeLHS -- DSP ASR, defined by GCC/Clang
    acc >>= 1;
    *e += 1;
  }
  return acc;
}

/* The tail at P:0x0B3F/P:0x0B81: CLB, NORMF, RND, the exponent update and the
 * P:0x0BBD rail. */
static gm2_swf swf_tail(int64_t acc, int e) {
  if (acc == 0) {
    return gm2_swf_zero;
  }
  acc = swf_overshoot(acc, &e);
  acc = swf_rnd(swf_normf(acc, &e));
  acc = swf_overshoot(acc, &e); /* the rounding can carry just past the field */
  acc = swf_normf(acc, &e);     /* or leave the -0x400000 gap on the way down */
  if (e & 0x100) { /* P:0x0BBD: an exponent past 255 rails to its 8 bits */
    if (e & 0x200) {
      return gm2_swf_zero;
    }
    gm2_swf rail = gm2_swf_from_words(acc < 0 ? 0x800000u : 0x7FFFFFu, 0xFFu);
    return rail;
  }
  gm2_swf r;
  r.m = swf_acc_mantissa(acc);
  r.e = e;
  return r;
}

/* --- primitives ---------------------------------------------------------- */

gm2_swf gm2_swf_from_words(uint32_t mantissa_word, uint32_t exponent_word) {
  gm2_swf r;
  r.m = gm2_s24(mantissa_word);
  r.e = (int)(exponent_word & 0xFFu);
  return r;
}

gm2_swf gm2_swf_from_int(int32_t n) { /* P:0x000BFD */
  if (n == 0) {
    return gm2_swf_zero;
  }
  return swf_tail((int64_t)gm2_s24((uint32_t)n) * SWF_POW24, 150);
}

gm2_swf gm2_swf_from_q23(int32_t word) { /* P:0x000C36, into the P:0x000B81 tail */
  return swf_tail((int64_t)gm2_s24((uint32_t)word) * SWF_POW24, 0x7F);
}

int32_t gm2_swf_to_int(gm2_swf a) { /* P:0x000BC6 */
  if (a.m == 0) {
    return 0;
  }
  if (a.e <= 0x7F) {
    /* P:0x0168 clears the accumulator for anything below 1.0; exactly -1.0 at
     * P:0x0BD5 still converts to -1. */
    return (a.e == 0x7F && a.m == GM2_W24_MIN) ? -1 : 0;
  }
  int shift = a.e - 150;
  if (shift > 0) { /* P:0x0BDB rails to 24 bits */
    return a.m > 0 ? GM2_W24_MAX : GM2_W24_MIN;
  }
  int64_t acc = (int64_t)a.m * SWF_POW24;
  acc = acc < 0 ? -acc : acc;
  acc >>= -shift;
  int32_t whole = swf_acc_mantissa(acc); /* A1, taken before the sign goes back on */
  return a.m < 0 ? -whole : whole;
}

int32_t gm2_swf_to_q23(gm2_swf a) { /* P:0x000C1C */
  if (a.m == 0) {
    return 0;
  }
  int shift = 127 - a.e;
  if (shift < 0) {
    return a.m > 0 ? GM2_W24_MAX : GM2_W24_MIN;
  }
  if (shift == 0) {
    return a.m;
  }
  if (shift > 22) { /* P:0x0168 */
    return 0;
  }
  return swf_acc_mantissa(swf_rnd((int64_t)a.m * SWF_POW24 >> shift)); /* ASR, then RND */
}

/* P:0x000BE1: the same truncation, but into the whole 56-bit accumulator, so
 * values past 24 bits survive. */
int64_t gm2_swf_to_int48(gm2_swf a) {
  if (a.m == 0) {
    return 0;
  }
  if (a.e <= 0x7F) {
    return (a.e == 0x7F && a.m == GM2_W24_MIN) ? -1 : 0;
  }
  int shift = 174 - a.e;
  if (shift < 0) { /* past the 48-bit field: the accumulator holds the rail */
    return a.m > 0 ? 0x7FFFFFFFFFFFLL : -((int64_t)1 << 47);
  }
  int64_t acc = (int64_t)a.m * SWF_POW24;
  acc = acc < 0 ? -acc : acc;
  acc >>= shift;
  return a.m < 0 ? -acc : acc;
}

/* P:0x000C07: the 48-bit integer held in A1:A0 to float. The firmware's base
 * exponent 0xAE is 150 plus the 24 bits of A1, so the accumulator enters this
 * tail unscaled. */
gm2_swf gm2_swf_from_int48(int64_t v) {
  if (v == 0) {
    return gm2_swf_zero;
  }
  return swf_tail(gm2_acc56(v), 174);
}

gm2_swf gm2_swf_add(gm2_swf a, gm2_swf b) { /* P:0x000B65 */
  int e = a.e > b.e ? a.e : b.e;
  int d = a.e > b.e ? a.e - b.e : b.e - a.e;
  if (d > 23) { /* the smaller operand is beyond the 24-bit grid */
    return a.e > b.e ? a : b;
  }
  int64_t acc;
  if (a.e > b.e) {
    acc = (int64_t)a.m * SWF_POW24 + (((int64_t)b.m * SWF_POW24) >> d);
  } else if (b.e > a.e) {
    acc = (int64_t)b.m * SWF_POW24 + (((int64_t)a.m * SWF_POW24) >> d);
  } else {
    acc = (int64_t)a.m * SWF_POW24 + (int64_t)b.m * SWF_POW24;
  }
  return swf_tail(acc, e);
}

/* P:0x000B5F subtracts its accumulator operand from its X operand, so a - b
 * negates b into the accumulator first. The subtract prologue normalises that
 * negation without the rounding step; for a 24-bit mantissa the two agree. */
gm2_swf gm2_swf_sub(gm2_swf a, gm2_swf b) { return gm2_swf_add(gm2_swf_neg(b), a); }

gm2_swf gm2_swf_mul(gm2_swf a, gm2_swf b) { /* P:0x000B8B */
  return swf_tail(2 * (int64_t)a.m * (int64_t)b.m, a.e + b.e - 0x7F);
}

gm2_swf gm2_swf_div(gm2_swf a, gm2_swf b) { /* P:0x000B9B */
  if (b.m == 0) { /* P:0x0BB5: a zero divisor rails, a zero dividend does not */
    if (a.m == 0) {
      return swf_tail(0, a.e - b.e + SWF_ONE_E);
    }
    gm2_swf r;
    r.m = a.m < 0 ? GM2_W24_MIN : GM2_W24_MAX; /* the zero divisor has no sign */
    r.e = 0xFF;
    return r;
  }
  int64_t na = a.m < 0 ? -(int64_t)a.m : (int64_t)a.m;
  int64_t nb = b.m < 0 ? -(int64_t)b.m : (int64_t)b.m;
  /* 24 DIV iterations at P:0x0BA6 leave the quotient truncated to the signed
   * 24-bit field it develops in, so an exact 2.0 stops at 0x7FFFFF. */
  int64_t q = (na * 0x400000LL) / nb;
  if (q > 0x7FFFFF) {
    q = 0x7FFFFF;
  }
  if ((a.m < 0) != (b.m < 0)) {
    q = -q;
  }
  return swf_tail(q * SWF_POW24, a.e - b.e + SWF_ONE_E);
}

/* P:0x000BBA */
gm2_swf gm2_swf_neg(gm2_swf a) { return swf_tail(-((int64_t)a.m * SWF_POW24), a.e); }

int gm2_swf_cmp(gm2_swf a, gm2_swf b) { /* P:0x000C39 */
  if ((a.m < 0) != (b.m < 0)) {
    return a.m < 0 ? -1 : 1;
  }
  /* Same sign, zero included: the exponent word decides, and only equal
   * exponents compare the mantissas. */
  if (a.e != b.e) {
    return (a.e < b.e) == (a.m >= 0) ? -1 : 1;
  }
  return a.m == b.m ? 0 : (a.m < b.m ? -1 : 1);
}

gm2_swf gm2_swf_mac(gm2_swf a, gm2_swf b, gm2_swf c) { /* P:0x000B37 */
  return gm2_swf_add(swf_tail(2 * (int64_t)b.m * (int64_t)c.m, b.e + c.e - 0x7F), a);
}

gm2_swf gm2_swf_msub(gm2_swf a, gm2_swf b, gm2_swf c) { /* P:0x000B4A */
  return gm2_swf_sub(a, swf_tail(2 * (int64_t)b.m * (int64_t)c.m, b.e + c.e - 0x7F));
}

/* P:0x097026 */
gm2_swf gm2_swf_abs(gm2_swf a) { return a.m < 0 ? gm2_swf_neg(a) : a; }

/* P:0x0970D3: the fraction of a, its integer part through `integer`. Values of
 * 2^23 or more are left whole. */
static gm2_swf swf_modf(gm2_swf a, gm2_swf *integer) {
  static const gm2_swf k_top = GM2_SWF_CONST(0x7FFFFFu, 0x96u); /* 8388607 */
  static const gm2_swf k_bottom = GM2_SWF_CONST(0x800000u, 0x96u); /* -8388608 */
  if (gm2_swf_cmp(a, k_top) > 0 || gm2_swf_cmp(a, k_bottom) < 0) {
    *integer = a;
    return gm2_swf_zero;
  }
  *integer = gm2_swf_from_int(gm2_swf_to_int(a));
  return gm2_swf_sub(a, *integer);
}

/* P:0x09757D: split a into a fraction in [0.5, 1) and the power of two it is
 * scaled by, |a| = fraction * 2^k. */
static gm2_swf swf_split(gm2_swf a, int *k) {
  *k = a.e - 0x7F;
  return gm2_swf_from_words((uint32_t)a.m, 0x7Fu);
}

/* P:0x09702A: floor towards minus infinity. */
static gm2_swf swf_floor(gm2_swf a) {
  gm2_swf whole;
  if (a.m >= 0) {
    (void)swf_modf(a, &whole);
    return whole;
  }
  gm2_swf frac = swf_modf(gm2_swf_neg(a), &whole);
  if (frac.m != 0) {
    whole = gm2_swf_add(whole, gm2_swf_one);
  }
  return gm2_swf_neg(whole);
}

/* P:0x0973C9: a * 2^n, the exponent range adjustment the exponential ends
 * with. */
static gm2_swf swf_ldexp(gm2_swf a, int n) {
  static const gm2_swf k_two30 = GM2_SWF_CONST(0x400000u, 0x9Eu); /* 1073741824 */
  int k;
  gm2_swf x = swf_split(a, &k);
  int e = k + n;
  if (n > 0x7F || e > 0x7F) { /* P:0x0973E2: the exponent would leave its byte */
    gm2_swf r = gm2_swf_from_words(0x7FFFFFu, 0xFFu);
    return r;
  }
  if (e < -125) { /* P:0x0973F7: below the smallest exponent, the result is 0 */
    return gm2_swf_zero;
  }
  while (e > 30) { /* 2^30 steps keep every product inside the exponent byte */
    x = gm2_swf_mul(x, k_two30);
    e -= 30;
  }
  while (e < -30) {
    x = gm2_swf_div(x, k_two30);
    e += 30;
  }
  if (e > 0) {
    x = gm2_swf_mul(x, gm2_swf_from_int48((int64_t)1 << e));
  } else if (e < 0) {
    x = gm2_swf_div(x, gm2_swf_from_int48((int64_t)1 << -e));
  }
  return x;
}

/* --- transcendental helpers ---------------------------------------------- */

gm2_swf gm2_swf_exp(gm2_swf a) { /* P:0x096FAA */
  static const gm2_swf k_low = GM2_SWF_CONST(0xA70000u, 0x86u); /* -89 */
  static const gm2_swf k_high = GM2_SWF_CONST(0x590000u, 0x86u); /* 89 */
  static const gm2_swf k_log2e = GM2_SWF_CONST(0x5C551Eu, 0x80u); /* 1.4426951 */
  static const gm2_swf k_c1 = GM2_SWF_CONST(0x7371D3u, 0x82u); /* 7.2152891 */
  static const gm2_swf k_c2 = GM2_SWF_CONST(0x762636u, 0x7Bu); /* 0.057690069 */
  static const gm2_swf k_c3 = GM2_SWF_CONST(0x534694u, 0x84u); /* 20.818924 */
  static const gm2_swf k_sqrt2 = GM2_SWF_CONST(0x5A827Au, 0x80u); /* 1.4142137 */
  if (a.m == 0) {
    return gm2_swf_one;
  }
  if (gm2_swf_cmp(a, k_low) < 0) {
    return gm2_swf_zero;
  }
  if (gm2_swf_cmp(a, k_high) > 0) {
    gm2_swf rail = gm2_swf_from_words(0x7FFFFFu, 0xFFu);
    return rail;
  }
  gm2_swf p = gm2_swf_mul(a, k_log2e);
  int n = gm2_swf_to_int(swf_floor(p));
  gm2_swf f = gm2_swf_sub(p, gm2_swf_from_int(n));
  gm2_swf u = gm2_swf_sub(f, gm2_swf_half);
  gm2_swf u2 = gm2_swf_mul(u, u);
  gm2_swf num = gm2_swf_mul(gm2_swf_mac(k_c1, k_c2, u2), u);
  gm2_swf den = gm2_swf_add(k_c3, u2);
  gm2_swf scaled = gm2_swf_mul(gm2_swf_add(den, num), k_sqrt2);
  gm2_swf r = gm2_swf_div(scaled, gm2_swf_sub(den, num));
  return swf_ldexp(r, n);
}

gm2_swf gm2_swf_ln(gm2_swf a) { /* P:0x097052 */
  static const gm2_swf k_inv_sqrt2 = GM2_SWF_CONST(0x5A827Au, 0x7Fu); /* 0.70710683 */
  static const gm2_swf k_c5 = GM2_SWF_CONST(0x72A113u, 0x7Fu); /* 0.8955406 */
  static const gm2_swf k_c6 = GM2_SWF_CONST(0x95F759u, 0x81u); /* -3.3135562 */
  static const gm2_swf k_c4 = GM2_SWF_CONST(0x95F75Au, 0x80u); /* -1.6567779 */
  static const gm2_swf k_ln2 = GM2_SWF_CONST(0x58B90Cu, 0x7Fu); /* 0.69314718 */
  if (a.m <= 0) { /* P:0x097056 */
    gm2_swf rail = gm2_swf_from_words(0x800001u, 0xFFu);
    return rail;
  }
  int k;
  gm2_swf y = swf_split(a, &k);
  if (gm2_swf_cmp(y, k_inv_sqrt2) < 0) { /* keep the series argument in [1/sqrt2, sqrt2) */
    y = gm2_swf_mul(y, gm2_swf_two);
    k -= 1;
  }
  gm2_swf z = gm2_swf_div(gm2_swf_sub(y, gm2_swf_one), gm2_swf_add(y, gm2_swf_one));
  gm2_swf z2 = gm2_swf_mul(z, z);
  gm2_swf num = gm2_swf_mac(k_c6, k_c5, z2);
  gm2_swf den = gm2_swf_add(k_c4, z2);
  gm2_swf ln_y = gm2_swf_mul(gm2_swf_div(num, den), z);
  return gm2_swf_mac(ln_y, gm2_swf_from_int(k), k_ln2);
}

gm2_swf gm2_swf_pow(gm2_swf a, gm2_swf b) { /* P:0x0970F1 */
  if (a.m > 0) {
    return gm2_swf_exp(gm2_swf_mul(gm2_swf_ln(a), b));
  }
  if (a.m == 0) { /* P:0x0970F7 */
    return gm2_swf_zero;
  }
  /* A negative base needs an exact integer exponent; the result takes its sign
   * from that exponent's parity. */
  int64_t n = gm2_swf_to_int48(b) & (int64_t)0xFFFFFFFFFFFF; /* A1:A0 */
  n = (n & ((int64_t)1 << 47)) != 0 ? n - ((int64_t)1 << 48) : n;
  gm2_swf back = gm2_swf_from_int48(n);
  if (back.m != b.m || back.e != b.e) { /* P:0x09710E */
    return gm2_swf_zero;
  }
  gm2_swf r = gm2_swf_exp(gm2_swf_mul(gm2_swf_ln(gm2_swf_neg(a)), b));
  return (n & 1) != 0 ? gm2_swf_neg(r) : r;
}

gm2_swf gm2_swf_sqrt(gm2_swf a) { /* P:0x0971DA */
  static const gm2_swf k_two30 = GM2_SWF_CONST(0x400000u, 0x9Eu); /* 1073741824 */
  if (a.m == 0) {
    return gm2_swf_zero;
  }
  if (a.m < 0) { /* P:0x0971E0 */
    return gm2_swf_zero;
  }
  int k;
  gm2_swf y = swf_split(a, &k);
  if ((k & 1) != 0) { /* an odd power of two has no exact square root */
    y = gm2_swf_mul(y, gm2_swf_two);
    k -= 1;
  }
  gm2_swf g = gm2_swf_mul(gm2_swf_add(y, gm2_swf_one), gm2_swf_half);
  /* The Newton step is the firmware's: it divides the argument, not g, so the
   * first estimate only seeds the iteration. The exponent moves between g and k
   * in whole 60s, which is 2^30 per step, to keep both inside the exponent
   * byte. */
  int q = (k - 1) / 60;
  while (q > 0) {
    g = gm2_swf_mul(g, k_two30);
    k -= 60;
    q -= 1;
  }
  q = (-1 - k) / 60;
  while (q > 0) {
    g = gm2_swf_div(g, k_two30);
    k += 60;
    q -= 1;
  }
  gm2_swf x;
  if (k >= 0) {
    x = gm2_swf_mul(g, gm2_swf_from_int48((int64_t)1 << (k >> 1)));
  } else {
    x = gm2_swf_div(g, gm2_swf_from_int48((int64_t)1 << ((-k) >> 1)));
  }
  for (int i = 0; i < 5; i++) {
    x = gm2_swf_mul(gm2_swf_add(gm2_swf_div(a, x), x), gm2_swf_half);
  }
  return x;
}

gm2_swf gm2_swf_sinh(gm2_swf a) { /* P:0x097147 */
  static const gm2_swf k_big =
      GM2_SWF_CONST(0x4A0000u, 0x85u); /* 37, past which e^-a is below the grid */
  static const gm2_swf k_neg_ln2 = GM2_SWF_CONST(0xA746F4u, 0x7Fu); /* -0.69314718 */
  static const gm2_swf k_quad = GM2_SWF_CONST(0xB44845u, 0x81u); /* -2.366178 */
  static const gm2_swf k_base = GM2_SWF_CONST(0xAF3725u, 0x84u); /* -20.196148 */
  int sign = 1;
  if (a.m < 0) { /* the routine works on |a| and puts the sign back at the end */
    a = gm2_swf_neg(a);
    sign = -1;
  }
  if (gm2_swf_cmp(a, k_big) > 0) { /* P:0x097161: sinh(a) = exp(a - ln2) */
    /* P:0x09716D tests the sign flag for equality with zero before negating, and
     * that flag is 1 or m0, never 0: for |a| > 37 the firmware returns the value
     * of |a|. */
    return gm2_swf_exp(gm2_swf_add(a, k_neg_ln2));
  }
  if (gm2_swf_cmp(a, gm2_swf_half) > 0) { /* P:0x09717F: (e^a - e^-a) / 2 */
    gm2_swf e1 = gm2_swf_exp(gm2_swf_add(a, k_neg_ln2));
    gm2_swf e2 = gm2_swf_exp(gm2_swf_add(gm2_swf_neg(a), k_neg_ln2));
    gm2_swf r = gm2_swf_sub(e1, e2);
    return sign < 0 ? gm2_swf_neg(r) : r;
  }
  /* P:0x0971A7: a * (base + quad*a^2) / (base + a^2) */
  gm2_swf a2 = gm2_swf_mul(a, a);
  gm2_swf num = gm2_swf_mul(gm2_swf_mac(k_base, k_quad, a2), a);
  gm2_swf r = gm2_swf_div(num, gm2_swf_add(k_base, a2));
  return sign < 0 ? gm2_swf_neg(r) : r;
}

/* P:0x0974A2: the rational approximation of atan the small-range branch uses. */
static gm2_swf swf_atan_poly(gm2_swf v) {
  static const gm2_swf k_c1 = GM2_SWF_CONST(0x6547A7u, 0x83u); /* 12.659986 */
  static const gm2_swf k_c2 = GM2_SWF_CONST(0x65E833u, 0x82u); /* 6.3691893 */
  static const gm2_swf k_c3 = GM2_SWF_CONST(0x54B680u, 0x83u); /* 10.589111 */
  gm2_swf v2 = gm2_swf_mul(v, v);
  gm2_swf num = gm2_swf_mac(k_c1, k_c2, v2);
  gm2_swf den = gm2_swf_mac(k_c1, v2, gm2_swf_add(k_c3, v2));
  return gm2_swf_mul(gm2_swf_div(num, den), v);
}

/* P:0x097464: atan for a positive argument. Under sqrt(2)-1 the rational
 * approximation is used directly; above sqrt(2)+1 it runs on 1/x and the result
 * is pi/2 minus that, and in between the argument moves to (x-1)/(x+1) around
 * pi/4. */
static gm2_swf swf_atan_pos(gm2_swf a) {
  static const gm2_swf k_quarter_lo = GM2_SWF_CONST(0x6A09E6u, 0x7Eu); /* sqrt(2)-1 */
  static const gm2_swf k_quarter_hi = GM2_SWF_CONST(0x4D413Du, 0x81u); /* sqrt(2)+1 */
  static const gm2_swf k_pi_2 = GM2_SWF_CONST(0x6487EEu, 0x80u); /* 1.5707965 */
  static const gm2_swf k_pi_4 = GM2_SWF_CONST(0x6487EDu, 0x7Fu); /* 0.78539813 */
  if (gm2_swf_cmp(a, k_quarter_lo) < 0) {
    return swf_atan_poly(a);
  }
  if (gm2_swf_cmp(a, k_quarter_hi) > 0) {
    return gm2_swf_sub(k_pi_2, swf_atan_poly(gm2_swf_div(gm2_swf_one, a)));
  }
  gm2_swf u = gm2_swf_sub(a, gm2_swf_one);
  return gm2_swf_add(k_pi_4, swf_atan_poly(gm2_swf_div(u, gm2_swf_add(a, gm2_swf_one))));
}

gm2_swf gm2_swf_atan(gm2_swf a) { /* P:0x096F90: atan is odd, so it negates */
  if (a.m >= 0) {
    return swf_atan_pos(a);
  }
  return gm2_swf_neg(swf_atan_pos(gm2_swf_neg(a)));
}

gm2_swf gm2_swf_tan(gm2_swf a) { /* P:0x09726D */
  static const gm2_swf k_four_over_pi = GM2_SWF_CONST(0x517CC2u, 0x80u); /* 1.2732396 */
  static const gm2_swf k_c1 = GM2_SWF_CONST(0x439DE7u, 0x88u); /* 270.46722 */
  static const gm2_swf k_c2 = GM2_SWF_CONST(0xB86769u, 0x86u); /* -71.596054 */
  static const gm2_swf k_c3 = GM2_SWF_CONST(0x6A3655u, 0x87u); /* 212.42447 */
  static const gm2_swf k_c4 = GM2_SWF_CONST(0x9B92D9u, 0x83u); /* -12.553297 */
  int sign = 1;
  if (a.m < 0) {
    a = gm2_swf_neg(a);
    sign = -1;
  }
  gm2_swf v = gm2_swf_mul(a, k_four_over_pi);
  gm2_swf whole;
  gm2_swf t = swf_modf(v, &whole);
  int quad = gm2_swf_to_int(whole) & 3;
  int reciprocal = 0;
  /* tan(x) = tan(pi/4 * (quad + fraction)): quad 1 and 3 mirror the fraction,
   * quad 1 and 2 turn the tangent into its cotangent, quad 2 and 3 flip the
   * sign. */
  if (quad == 1 || quad == 3) {
    t = gm2_swf_sub(gm2_swf_one, t);
  }
  if (quad == 1 || quad == 2) {
    reciprocal = 1;
  }
  if (quad == 2 || quad == 3) {
    sign = -sign;
  }
  gm2_swf t2 = gm2_swf_mul(t, t);
  gm2_swf num = gm2_swf_mul(gm2_swf_mac(k_c3, k_c4, t2), t);
  gm2_swf r = gm2_swf_div(num, gm2_swf_mac(k_c1, t2, gm2_swf_add(k_c2, t2)));
  if (reciprocal) {
    if (r.m == 0) { /* the pole: 1/0, railed to the sign */
      gm2_swf rail = gm2_swf_from_words(sign < 0 ? 0x800001u : 0x7FFFFFu, 0xFFu);
      return rail;
    }
    r = gm2_swf_div(gm2_swf_one, r);
  }
  return sign < 0 ? gm2_swf_neg(r) : r;
}

gm2_swf gm2_swf_hz20(int raw) { /* P:0x08F0DF */
  static const gm2_swf k_step = GM2_SWF_CONST(0x666666u, 0x7Bu); /* 1/20 */
  return gm2_swf_pow(gm2_swf_ten, gm2_swf_mac(gm2_swf_one, gm2_swf_from_int(raw + 6), k_step));
}

gm2_swf gm2_swf_hz80(int raw) { /* P:0x08F0F8 */
  static const gm2_swf k_step = GM2_SWF_CONST(0x666666u, 0x79u); /* 1/80 */
  return gm2_swf_pow(gm2_swf_ten, gm2_swf_mac(gm2_swf_one, gm2_swf_from_int(raw + 24), k_step));
}

/* P:0x0974D5: sin (quad_offset = 0) and cos (quad_offset = 1 on |a|). */
static gm2_swf swf_sincos(gm2_swf a, int quad_offset) {
  static const gm2_swf k_two_over_pi = GM2_SWF_CONST(0x517CC2u, 0x7Fu); /* 0.63661981 */
  static const gm2_swf k_large_limit = GM2_SWF_CONST(0x7FFC00u, 0x8Eu); /* 32764 */
  static const gm2_swf k_n0 = GM2_SWF_CONST(0xA90D72u, 0x87u); /* -173.89496 */
  static const gm2_swf k_n1 = GM2_SWF_CONST(0x536259u, 0x83u); /* 10.423021 */
  static const gm2_swf k_n2 = GM2_SWF_CONST(0x4229A6u, 0x89u); /* 529.30151 */
  static const gm2_swf k_d0 = GM2_SWF_CONST(0x6F7689u, 0x84u); /* 27.865757 */
  static const gm2_swf k_d1 = GM2_SWF_CONST(0x543DAFu, 0x88u); /* 336.96381 */
  if (a.m < 0) {
    a = gm2_swf_neg(a);
    quad_offset += 2;
  }
  gm2_swf u = gm2_swf_mul(k_two_over_pi, a);
  gm2_swf t;
  int n5;
  if (gm2_swf_cmp(u, k_large_limit) > 0) {
    gm2_swf whole;
    t = swf_modf(u, &whole);
    gm2_swf s65 = gm2_swf_add(gm2_swf_from_int(quad_offset), whole);
    gm2_swf q = gm2_swf_mul(gm2_swf_quarter, s65);
    gm2_swf whole2;
    (void)swf_modf(q, &whole2);
    n5 = gm2_swf_to_int(gm2_swf_msub(s65, gm2_swf_four, whole2));
  } else {
    int32_t y1 = gm2_swf_to_int(u);
    t = gm2_swf_sub(u, gm2_swf_from_int(y1));
    n5 = (quad_offset + y1) & 3;
  }
  if ((n5 & 1) != 0) {
    t = gm2_swf_sub(gm2_swf_one, t);
  }
  if (n5 > 1) {
    t = gm2_swf_neg(t);
  }
  gm2_swf t2 = gm2_swf_mul(t, t);
  gm2_swf num = gm2_swf_mac(k_n0, k_n1, t2);
  num = gm2_swf_mac(k_n2, t2, num);
  num = gm2_swf_mul(num, t);
  gm2_swf den = gm2_swf_add(k_d0, t2);
  den = gm2_swf_mac(k_d1, t2, den);
  return gm2_swf_div(num, den);
}

gm2_swf gm2_swf_sin(gm2_swf a) { return swf_sincos(a, 0); } /* P:0x097140 */

gm2_swf gm2_swf_cos(gm2_swf a) { /* P:0x096F9F */
  return swf_sincos(a.m < 0 ? gm2_swf_neg(a) : a, 1);
}

gm2_swf gm2_swf_db(int32_t db) { /* P:0x082E01 */
  static const gm2_swf k_ln10_over_20 = GM2_SWF_CONST(0x75E471u, 0x7Cu); /* 0.11512925 */
  return gm2_swf_exp(gm2_swf_mul(gm2_swf_from_int(db), k_ln10_over_20));
}

int32_t gm2_swf_db_to_lin(int32_t db) { /* P:0x082DF1 */ return gm2_swf_to_q23(gm2_swf_db(db)); }
