/* The resonance filters' coefficient design on the firmware's software float.
 *
 * With S the Sense value, R the Res value, T the Response value and M the FrqMax
 * value, the words are
 *
 *   P:0x08F0DF  Hz(raw) = 10^(1 + (raw + 6)/20)
 *   P:0x091F81  denom = {0x53AE15, 0x82} for Style 0, {0x5BD70A, 0x81} for 1
 *               q     = 0.7 * 2^(R * (Style ? 0.005 : 0.01) * {0x4D624F, 0x82})
 *               UI+42/43 = denom,  UI+44/45 = sqrt(0.7 / denom)
 *               UI+46/47 = q,      UI+48/49 = sqrt(0.7 / q)
 *   P:0x091E7B  w0 = 2 rate tan(pi f / rate), from the clamped f and Q
 *               rows = (1, w, w^2), (2, 0, -2 w^2), (1, -w, w^2) times
 *                      (w0^2, w0 / Q, 1), w = 2 rate
 *   P:0x09208E  coef[0] = to_q23(0.5 Y / 4), [1] and [2] = to_q23(0.25 Y / 4),
 *               [3] = to_q23(-(1 + row1 / row0) / 4),
 *               [4] = to_q23(-(row2 / row0) / 2), with
 *               Y = scale * (1 + (row1 + row2) / row0)
 *   P:0x091FF2  coef[5..9] = coef[0..4] for Style 1 and the fixed
 *               (0, 0, 0x200000, -0x200000, 0) set for Style 0; coef[10..19] is
 *               a copy of coef[0..9]
 *   P:0x091DC2  the follower's coeff and threshold, from Response (gm2_tick.h)
 *   P:0x091D78  UI+40/41 = 1 / (y - s) and UI+38/39 = -s * UI+40/41, from
 *               Sense, with s = (10 - S) * {0x5D1746, 0x7C} and
 *               y = min(1, 0.6 + s) (gm2_tick.h)
 *   P:0x091CF9  level = from_q23(X:0x0B03), the follower steps with coeff and
 *               threshold, and the design runs at
 *               Hz(M) * 10^((2.5 - 1) (1 + UI+38/39 + UI+40/41 * S)) as UI+42/43
 *               and UI+44/45 give Q and the tail, clamped into 10^2.5 Hz..Hz(M)
 */
#include "gm2_res_design.h"

#include "gm2_dsp_math.h"
#include "gm2_rate.h"
#include "gm2_swf.h"
#include "gm2_tick.h"

/* The firmware's operands as (mantissa, exponent) words. */
static const gm2_swf k_two_pi = GM2_SWF_CONST(0x6487EEu, 0x82u); /* 2 pi */
static const gm2_swf k_q_low = GM2_SWF_CONST(0x59999Au, 0x7Fu);    /* 0.7 */
static const gm2_swf k_hz_low = GM2_SWF_CONST(0x4B0000u, 0x87u);   /* 150 Hz */
static const gm2_swf k_tick_low = GM2_SWF_CONST(0x4F0CCDu, 0x88u); /* 10^2.5 Hz */
static const gm2_swf k_style_q[2] = {GM2_SWF_CONST(0x53AE15u, 0x82u), /* 5.23 */
                                     GM2_SWF_CONST(0x5BD70Au, 0x81u)}; /* 2.87 */
static const gm2_swf k_res_cent = GM2_SWF_CONST(0x51EB85u, 0x79u); /* 0.01 */
static const gm2_swf k_res_span = GM2_SWF_CONST(0x4D624Fu, 0x82u); /* 4.840267 */

/* The biquad set the second-order Style leaves the second biquad at. */
static const int32_t k_fixed_biquad[GM2_BIQUAD_COEFS] = {0x000000, 0x000000, 0x200000, -0x200000,
                                                         0x000000};

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  RESONANCE_MIN = 0,
  RESONANCE_MAX = 100,
  FREQ_MAX_MIN = 34,
  FREQ_MAX_MAX = 54,
};

gm2_res_design_shape gm2_res_design_resonance(int style, int resonance) {
  const unsigned which = style != 0 ? 1u : 0u;
  const int res = gm2_clamp_raw(resonance, RESONANCE_MIN, RESONANCE_MAX);
  gm2_res_design_shape shape;
  gm2_swf res_f = gm2_swf_from_int(res);
  if (which != 0u) {
    res_f = gm2_swf_mul(res_f, gm2_swf_half);
  }
  shape.q = gm2_swf_mul(
      k_q_low, gm2_swf_pow(gm2_swf_two, gm2_swf_mul(gm2_swf_mul(res_f, k_res_cent), k_res_span)));
  shape.scale = gm2_swf_sqrt(gm2_swf_div(k_q_low, shape.q));
  return shape;
}

gm2_res_design_shape gm2_res_design_touch(int style) {
  gm2_res_design_shape shape;
  shape.q = k_style_q[style != 0];
  shape.scale = gm2_swf_sqrt(gm2_swf_div(k_q_low, shape.q));
  return shape;
}

/* The five words of one biquad for a frequency, a Q and a tail multiplier, with
 * the rate words the transform's w = 2 rate comes from. */
static void res_design_biquad(const gm2_rate_words *rate, gm2_swf hz, gm2_res_design_shape shape,
                              int32_t five[GM2_BIQUAD_COEFS]) {
  const gm2_swf qc = gm2_swf_cmp(shape.q, k_q_low) < 0 ? k_q_low : shape.q;
  const gm2_swf fc = gm2_swf_cmp(hz, k_hz_low) < 0 ? k_hz_low : hz;

  /* P:0x091ED2: w0 = 2 rate tan(pi f / rate). */
  gm2_swf t = gm2_swf_mul(fc, k_two_pi);
  t = gm2_swf_mul(t, gm2_swf_half);
  t = gm2_swf_mul(t, rate->period);
  t = gm2_swf_tan(t);
  t = gm2_swf_mul(t, gm2_swf_two);
  t = gm2_swf_mul(t, rate->rate);

  /* The prototype (w0^2, w0 / Q, 1) against the rate matrix of P:0x091E47. */
  const gm2_swf v[3] = {gm2_swf_mul(t, t), gm2_swf_div(t, qc), gm2_swf_one};
  const gm2_swf w = gm2_swf_mul(gm2_swf_two, rate->rate);
  const gm2_swf w2 = gm2_swf_mul(gm2_swf_mul(gm2_swf_four, rate->rate), rate->rate);
  const gm2_swf minus_2w2 = gm2_swf_mul(w2, gm2_swf_minus_two);
  const gm2_swf matrix[3][3] = {{gm2_swf_one, w, w2},
                                {gm2_swf_two, gm2_swf_zero, minus_2w2},
                                {gm2_swf_one, gm2_swf_neg(w), w2}};
  gm2_swf row[3];
  for (unsigned i = 0; i < 3u; i++) {
    gm2_swf acc = gm2_swf_zero;
    for (unsigned j = 0; j < 3u; j++) {
      acc = gm2_swf_mac(acc, v[j], matrix[i][j]);
    }
    row[i] = acc;
  }

  const gm2_swf ratio = gm2_swf_div(gm2_swf_one, row[0]);
  const gm2_swf y =
      gm2_swf_mul(gm2_swf_mac(gm2_swf_one, gm2_swf_add(row[1], row[2]), ratio), shape.scale);
  /* The caller's frame holds 0.5 Y in the second pair and 0.25 Y in the first and
   * third; P:0x09208E takes a quarter of each. */
  const gm2_swf amp = gm2_swf_mul(gm2_swf_quarter, y);
  const gm2_swf mid = gm2_swf_mul(gm2_swf_half, y);
  five[0] = gm2_swf_to_q23(gm2_swf_mul(gm2_swf_quarter, mid));
  five[1] = gm2_swf_to_q23(gm2_swf_mul(gm2_swf_quarter, amp));
  five[2] = five[1];
  five[3] = gm2_swf_to_q23(gm2_swf_mul(
      gm2_swf_quarter, gm2_swf_neg(gm2_swf_add(gm2_swf_one, gm2_swf_mul(row[1], ratio)))));
  five[4] = gm2_swf_to_q23(gm2_swf_mul(gm2_swf_half, gm2_swf_neg(gm2_swf_mul(row[2], ratio))));
}

void gm2_res_design_coefs(const gm2_rate_words *rate, int style, gm2_swf hz,
                          gm2_res_design_shape shape, int32_t coef[GM2_RES_CORE_COEFS]) {
  int32_t five[GM2_BIQUAD_COEFS];
  res_design_biquad(rate, hz, shape, five);
  for (unsigned i = 0; i < GM2_BIQUAD_COEFS; i++) {
    coef[i] = five[i];
    coef[GM2_BIQUAD_COEFS + i] = style != 0 ? five[i] : k_fixed_biquad[i];
  }
  for (unsigned i = 0; i < 2u * GM2_BIQUAD_COEFS; i++) {
    coef[2u * GM2_BIQUAD_COEFS + i] = coef[i];
  }
}

void gm2_res_design_tick_words_init(gm2_res_design_tick_words *words, int style, int response,
                                    int sense, int freq_max) {
  const int freq = gm2_clamp_raw(freq_max, FREQ_MAX_MIN, FREQ_MAX_MAX);
  gm2_tick_response(response, &words->coeff, &words->threshold);
  gm2_tick_sense(sense, &words->centre, &words->span);
  words->shape = gm2_res_design_touch(style);
  words->max_hz = gm2_swf_hz20(freq);
}

void gm2_res_design_tick(const gm2_rate_words *rate, const gm2_res_design_tick_words *words,
                         int style, gm2_swf *follower, int32_t level,
                         int32_t coef[GM2_RES_CORE_COEFS]) {
  gm2_swf state =
      gm2_tick_follow(*follower, gm2_swf_from_q23(level), words->threshold, words->coeff);
  *follower = state;
  const gm2_swf exponent = gm2_swf_mac(words->centre, state, words->span);
  const gm2_swf ln_range = gm2_swf_ln(gm2_swf_div(words->max_hz, k_tick_low));
  gm2_swf hz = gm2_swf_mul(gm2_swf_exp(gm2_swf_mul(ln_range, exponent)), k_tick_low);
  if (gm2_swf_cmp(hz, k_tick_low) < 0) {
    hz = k_tick_low;
  }
  if (gm2_swf_cmp(hz, words->max_hz) > 0) {
    hz = words->max_hz;
  }
  gm2_res_design_coefs(rate, style, hz, words->shape, coef);
}
