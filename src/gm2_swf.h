/* The firmware's software float, its transcendental helpers, and the primitives
 * they are built from, P:0x000B37..0x000C39 and P:0x096FAA..0x09757D.
 *
 * Every value is a pair (mantissa, exponent): a signed 24-bit fraction with
 * value = m * 2^(e - 150), normalised so that |m| is in [0x400000, 0x800000),
 * zero being m == 0. The routines below are word-exact ports of the
 * application-image routines named next to them: same mantissa, same exponent,
 * same half-to-even rounding.
 *
 * Operand order follows the DSP convention the firmware uses: the accumulator A
 * holds the first operand (mantissa A1, exponent B1) and the X register the
 * second (X1, X0). The header keeps the mathematical order instead, so
 * gm2_swf_sub(a, b) is a - b, and gm2_swf_cmp(a, b) orders a against b.
 *
 * The helpers report domain and range failures through their return value, zero
 * or the railed value. The error word X:0x0EB2 the firmware also writes is not
 * kept: no effect reads it.
 *
 * Firmware behaviour a caller inherits, reproduced here except where noted:
 *   - sinh returns sinh(|a|) above |a| = 37 (a sign-flag bug at P:0x09716D).
 *   - sqrt never returns when the exponent left after its range reduction is 60
 *     (exponents 0xBB, 0xBC, 0xF7, 0xF8 and values that reduce to them):
 *     P:0x097229 runs its loop 2^24-1 times. This port returns the converged
 *     value there instead.
 *   - an exact 2.0 quotient saturates to 0x7FFFFF, the largest the signed 24-bit
 *     quotient field holds.
 *   - a zero mantissa can carry an exponent: the arithmetic leaves {0, 0xFF}
 *     after a railed multiply, and the helpers treat it as a value, not as zero.
 */
#ifndef GM2_SWF_H
#define GM2_SWF_H

#include <stdint.h>

/* One value of the firmware's software float. */
typedef struct {
  int32_t m; /* signed 24-bit mantissa; |m| in [0x400000, 0x800000) when nonzero */
  int e;     /* exponent; value = m * 2^(e - 150) */
} gm2_swf;

/* A value from the pair the firmware writes, exponent word first in memory: the
 * mantissa word is signed 24-bit and the exponent word keeps its low eight bits. */
gm2_swf gm2_swf_from_words(uint32_t mantissa_word, uint32_t exponent_word);

/* The same pair as a constant initializer: the form every constant a setup names
 * takes. */
#define GM2_SWF_CONST(mantissa_word, exponent_word)                                                \
  {(int32_t)(((int64_t)((uint32_t)(mantissa_word) & 0xFFFFFFu) ^ 0x800000) - 0x800000),            \
   (int32_t)((uint32_t)(exponent_word) & 0xFFu)}

/* The exact values every setup reaches for. */
static const gm2_swf gm2_swf_zero = {0, 0};
static const gm2_swf gm2_swf_one = GM2_SWF_CONST(0x400000u, 0x80u);
static const gm2_swf gm2_swf_two = GM2_SWF_CONST(0x400000u, 0x81u);
static const gm2_swf gm2_swf_four = GM2_SWF_CONST(0x400000u, 0x82u);
static const gm2_swf gm2_swf_half = GM2_SWF_CONST(0x400000u, 0x7Fu);
static const gm2_swf gm2_swf_quarter = GM2_SWF_CONST(0x400000u, 0x7Eu);
static const gm2_swf gm2_swf_minus_one = GM2_SWF_CONST(0x800000u, 0x7Fu);
static const gm2_swf gm2_swf_minus_two = GM2_SWF_CONST(0x800000u, 0x80u);
static const gm2_swf gm2_swf_ten = GM2_SWF_CONST(0x500000u, 0x83u);
static const gm2_swf gm2_swf_hundred = GM2_SWF_CONST(0x640000u, 0x86u);

/* And the words the firmware rounds to. */
static const gm2_swf gm2_swf_hundredth = GM2_SWF_CONST(0x51EB85u, 0x79u);  /* 0.01 */
static const gm2_swf gm2_swf_thousandth = GM2_SWF_CONST(0x418937u, 0x76u); /* 0.001 */
static const gm2_swf gm2_swf_pi = GM2_SWF_CONST(0x6487EEu, 0x81u);         /* 3.1415930 */
static const gm2_swf gm2_swf_two_pi = GM2_SWF_CONST(0x6487EEu, 0x82u);     /* 6.2831860 */

/* P:0x000BFD: 24-bit integer in A1 to float. */
gm2_swf gm2_swf_from_int(int32_t n);

/* P:0x000C36: a 1.23 word, loaded with A0 clear, to float. */
gm2_swf gm2_swf_from_q23(int32_t word);

/* P:0x000BC6: float to a 24-bit integer, truncated toward zero and railed. */
int32_t gm2_swf_to_int(gm2_swf a);

/* P:0x000BE1: float to a 48-bit integer in the 56-bit accumulator. */
int64_t gm2_swf_to_int48(gm2_swf a);

/* P:0x000C07: 48-bit integer in A1:A0 to float. */
gm2_swf gm2_swf_from_int48(int64_t v);

/* P:0x000C1C: float to a 1.23 word, value * 2^23 rounded half to even, railed
 * from exponent 0x80 up and 0 below exponent 0x69. */
int32_t gm2_swf_to_q23(gm2_swf a);

/* P:0x000B65 add, P:0x000B5F subtract, P:0x000B8B multiply, P:0x000B9B divide,
 * P:0x000BBA negate. */
gm2_swf gm2_swf_add(gm2_swf a, gm2_swf b);
gm2_swf gm2_swf_sub(gm2_swf a, gm2_swf b);
gm2_swf gm2_swf_mul(gm2_swf a, gm2_swf b);
gm2_swf gm2_swf_div(gm2_swf a, gm2_swf b);
gm2_swf gm2_swf_neg(gm2_swf a);

/* P:0x000C39: -1, 0 or 1 as a is less than, equal to or greater than b. */
int gm2_swf_cmp(gm2_swf a, gm2_swf b);

/* P:0x000B37 and P:0x000B4A: a + b*c and a - b*c, the three-operand forms the
 * polynomial evaluations use. */
gm2_swf gm2_swf_mac(gm2_swf a, gm2_swf b, gm2_swf c);
gm2_swf gm2_swf_msub(gm2_swf a, gm2_swf b, gm2_swf c);

/* P:0x097026: |a|. */
gm2_swf gm2_swf_abs(gm2_swf a);

/* P:0x096FAA exp, P:0x097052 ln, P:0x0970F1 pow, P:0x0971DA sqrt, P:0x097147
 * sinh, P:0x096F90 atan, P:0x09726D tan, P:0x097140 sin, P:0x096F9F cos. */
gm2_swf gm2_swf_exp(gm2_swf a);
gm2_swf gm2_swf_ln(gm2_swf a);
gm2_swf gm2_swf_pow(gm2_swf a, gm2_swf b);
gm2_swf gm2_swf_sqrt(gm2_swf a);
gm2_swf gm2_swf_sinh(gm2_swf a);
gm2_swf gm2_swf_atan(gm2_swf a);
gm2_swf gm2_swf_tan(gm2_swf a);
gm2_swf gm2_swf_sin(gm2_swf a);
gm2_swf gm2_swf_cos(gm2_swf a);

/* P:0x082E01: 10^(db/20), and P:0x082DF1: the same as a 1.23 word. */
gm2_swf gm2_swf_db(int32_t db);
int32_t gm2_swf_db_to_lin(int32_t db);

/* P:0x08F0DF: 10^(1 + (raw + 6)/20), the frequency in Hz of a raw Hi-Cut, Lo-Cut
 * or FrqMax value: 20 steps a decade. */
gm2_swf gm2_swf_hz20(int raw);

/* P:0x08F0F8: 10^(1 + (raw + 24)/80), the frequency in Hz of a raw EQ Freq value
 * and, at Speed - 208, of an LFO Speed step: 80 steps a decade. */
gm2_swf gm2_swf_hz80(int raw);

#endif /* GM2_SWF_H */
