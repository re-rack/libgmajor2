/* The EQ's updater on the firmware's software float.
 *
 *   P:0x095C3C  the band's five floats: a peaking filter with a prescribed gain
 *               at half the sample rate, so that its response follows the analog
 *               one up there. With G the linear gain, GB^2 = sqrt(G)^2 the gain
 *               at the band edges, w = 2 pi f / rate and W the width in octaves:
 *
 *                 dw   = 2 w sinh(ln 2 * W / 2)
 *                 G1^2 = ((w^2 - pi^2)^2 + G^2 F) / ((w^2 - pi^2)^2 + F),
 *                        F = pi^2 dw^2 |GB^2 - 1| / |G^2 - GB^2|
 *                 W2   = sqrt(|G^2 - G1^2| / |G^2 - 1|) tan(w/2)^2
 *                 DW   = (1 + sqrt(|GB^2 - 1| / |GB^2 - G1^2|) W2) tan(band/2)
 *                 C    = DW^2 |GB^2 - G1^2|
 *                        - 2 W2 (|GB^2 - G1| - sqrt(|GB^2 - 1| |GB^2 - G1^2|))
 *                 D    = 2 W2 (|G^2 - G1| - sqrt(|G^2 - 1| |G^2 - G1^2|))
 *                 A    = sqrt((C + D) / |G^2 - GB^2|)
 *                 B    = sqrt((G^2 C + GB^2 D) / |G^2 - GB^2|)
 *                 f    = {G1 + W2 + B, -2 (G1 - W2), G1 - B + W2, -2 (1 - W2),
 *                         1 + W2 - A} / (1 + W2 + A)
 *
 *               where `band` is the bandwidth whose lower edge 2^(-W/2) w lands
 *               where the analog filter's does
 *   P:0x090620  the five words: the forward words a quarter of f1, f2 and f0,
 *               the first feedback word -(f3 + 1)/4 because the kernel adds the
 *               accumulator itself, and the second -f4/2
 */
#include "gm2_eq_setup.h"

#include "gm2_dsp_math.h"
#include "gm2_rate.h"
#include "gm2_swf.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  FREQ_MIN = 25,
  FREQ_MAX = 241,
  GAIN_MIN = -12,
  GAIN_MAX = 12,
  WIDTH_MIN = 3,
  WIDTH_MAX = 16,
};

/* The Width's zero and its steps an octave. */
enum { EQ_SETUP_WIDTH_OCTAVE = 10 };

static const gm2_swf k_two_pi = GM2_SWF_CONST(0x6487EEu, 0x82u);     /* 2 pi */
static const gm2_swf k_pi = GM2_SWF_CONST(0x6487EEu, 0x81u);         /* pi */
static const gm2_swf k_pi_squared = GM2_SWF_CONST(0x4EF4F4u, 0x83u); /* pi^2 */
static const gm2_swf k_third = GM2_SWF_CONST(0x555556u, 0x7Eu);      /* 1/3 */

/* P:0x095C3C from 0x095C49 on: the five floats of one band, or -1 where the
 * chain leaves the ported path. */
static int eq_chain(const gm2_rate_words *rate, gm2_swf gain, gm2_swf freq, gm2_swf width,
                    gm2_swf out[GM2_BIQUAD_COEFS]) {
  /* 95C50..95C6F: the frequency over the rate, and the angle.
   * shortcut: freq >= rate/2 (P:0x095D2D..P:0x095DD1) is not ported, port it
   * when rates below ~40 kHz must reach it */
  const gm2_swf turns = gm2_swf_mul(freq, rate->period);
  if (gm2_swf_cmp(turns, gm2_swf_half) >= 0) {
    return -1;
  }
  const gm2_swf omega = gm2_swf_mul(turns, k_two_pi);

  /* 95C6F..95C89: dw */
  gm2_swf dw = gm2_swf_mul(gm2_swf_ln(gm2_swf_two), width);
  dw = gm2_swf_sinh(gm2_swf_div(dw, gm2_swf_two));
  dw = gm2_swf_mul(dw, gm2_swf_mul(omega, gm2_swf_two));

  /* 95C8B..95CC4: G^2, GB^2 and their distances to each other and to one. */
  const gm2_swf root = gm2_swf_sqrt(gain);
  const gm2_swf g_sq = gm2_swf_mul(gain, gain);
  const gm2_swf gb_sq = gm2_swf_mul(root, root);
  const gm2_swf g_gb = gm2_swf_abs(gm2_swf_sub(g_sq, gb_sq));
  const gm2_swf g_one = gm2_swf_abs(gm2_swf_sub(g_sq, gm2_swf_one));
  const gm2_swf gb_one = gm2_swf_abs(gm2_swf_sub(gb_sq, gm2_swf_one));

  /* 95CC5..95D1F: G1 */
  gm2_swf curve = gm2_swf_sub(gm2_swf_mul(omega, omega), k_pi_squared);
  curve = gm2_swf_mul(curve, curve);
  const gm2_swf factor[4] = {k_pi, k_pi, dw, dw};
  gm2_swf top = gm2_swf_mul(g_sq, gb_one);
  gm2_swf bottom = gb_one;
  for (unsigned i = 0; i < 4u; i++) {
    top = gm2_swf_mul(top, factor[i]);
    bottom = gm2_swf_mul(bottom, factor[i]);
  }
  top = gm2_swf_add(gm2_swf_div(top, g_gb), curve);
  bottom = gm2_swf_add(gm2_swf_div(bottom, g_gb), curve);
  const gm2_swf g1 = gm2_swf_sqrt(gm2_swf_div(top, bottom));

  /* 95DD2..95E14: G1 and its square against the two gains. */
  const gm2_swf g1_sq = gm2_swf_mul(g1, g1);
  const gm2_swf g_g1 = gm2_swf_abs(gm2_swf_sub(g_sq, g1));
  const gm2_swf g_g1sq = gm2_swf_abs(gm2_swf_sub(g_sq, g1_sq));
  const gm2_swf gb_g1 = gm2_swf_abs(gm2_swf_sub(gb_sq, g1));
  const gm2_swf gb_g1sq = gm2_swf_abs(gm2_swf_sub(gb_sq, g1_sq));

  /* 95E15..95E70: tan(w/2), the lower band edge, and the bandwidth that puts the
   * upper edge where the two edges' product asks for it. */
  const gm2_swf centre = gm2_swf_tan(gm2_swf_div(omega, gm2_swf_two));
  const gm2_swf edge =
      gm2_swf_mul(gm2_swf_pow(gm2_swf_two, gm2_swf_div(gm2_swf_neg(width), gm2_swf_two)), omega);
  gm2_swf edges =
      gm2_swf_sqrt(gm2_swf_div(gm2_swf_mul(gb_one, g_g1sq), gm2_swf_mul(gb_g1sq, g_one)));
  edges = gm2_swf_mul(gm2_swf_mul(edges, centre), centre);
  gm2_swf band = gm2_swf_atan(gm2_swf_div(edges, gm2_swf_tan(gm2_swf_div(edge, gm2_swf_two))));
  band = gm2_swf_sub(gm2_swf_mul(band, gm2_swf_two), edge);

  /* 95E71..95EB9: W2 and DW^2 |GB^2 - G1^2| */
  gm2_swf w2 = gm2_swf_sqrt(gm2_swf_div(g_g1sq, g_one));
  w2 = gm2_swf_mul(gm2_swf_mul(w2, centre), centre);
  gm2_swf spread =
      gm2_swf_add(gm2_swf_one, gm2_swf_mul(w2, gm2_swf_sqrt(gm2_swf_div(gb_one, gb_g1sq))));
  spread = gm2_swf_mul(spread, gm2_swf_tan(gm2_swf_div(band, gm2_swf_two)));
  const gm2_swf spread_sq = gm2_swf_mul(gm2_swf_mul(spread, gb_g1sq), spread);
  const gm2_swf w2_twice = gm2_swf_mul(gm2_swf_two, w2);

  /* 95EBA..95F04: C, D, A and B */
  const gm2_swf c = gm2_swf_msub(
      spread_sq, gm2_swf_sub(gb_g1, gm2_swf_sqrt(gm2_swf_mul(gb_one, gb_g1sq))), w2_twice);
  const gm2_swf d =
      gm2_swf_mul(w2_twice, gm2_swf_sub(g_g1, gm2_swf_sqrt(gm2_swf_mul(g_one, g_g1sq))));
  const gm2_swf a = gm2_swf_sqrt(gm2_swf_div(gm2_swf_add(c, d), g_gb));
  const gm2_swf b = gm2_swf_sqrt(gm2_swf_div(gm2_swf_mac(gm2_swf_mul(g_sq, c), d, gb_sq), g_gb));

  /* 95F06..95F6C: the five floats. */
  const gm2_swf w2_one = gm2_swf_add(w2, gm2_swf_one);
  const gm2_swf norm = gm2_swf_div(gm2_swf_one, gm2_swf_add(w2_one, a));
  out[0] = gm2_swf_mul(gm2_swf_add(gm2_swf_add(g1, w2), b), norm);
  out[1] = gm2_swf_mul(gm2_swf_mul(gm2_swf_sub(g1, w2), gm2_swf_minus_two), norm);
  out[2] = gm2_swf_mul(gm2_swf_add(gm2_swf_sub(g1, b), w2), norm);
  out[3] = gm2_swf_mul(gm2_swf_mul(gm2_swf_sub(gm2_swf_one, w2), gm2_swf_minus_two), norm);
  out[4] = gm2_swf_mul(gm2_swf_sub(w2_one, a), norm);
  return 0;
}

/* P:0x0906B6 for one band: 0, or -1 where the chain leaves the ported path. */
static int eq_band(const gm2_rate_words *rate, int raw_freq, int raw_gain, int raw_width,
                   int32_t coef[GM2_BIQUAD_COEFS]) {
  const int freq = gm2_clamp_raw(raw_freq, FREQ_MIN, FREQ_MAX);
  const int gain = gm2_clamp_raw(raw_gain, GAIN_MIN, GAIN_MAX);
  const int width = gm2_clamp_raw(raw_width, WIDTH_MIN, WIDTH_MAX);

  /* P:0x09065A leaves a band that is Off or at 0 dB at the floats of a wire. */
  gm2_swf f[GM2_BIQUAD_COEFS] = {gm2_swf_one, gm2_swf_zero, gm2_swf_zero, gm2_swf_zero,
                                 gm2_swf_zero};
  if (freq < FREQ_MAX && gain != 0) {
    const gm2_swf octaves = gm2_swf_pow(
        gm2_swf_two, gm2_swf_mul(gm2_swf_from_int(width - EQ_SETUP_WIDTH_OCTAVE), k_third));
    if (eq_chain(rate, gm2_swf_db(gain), gm2_swf_hz80(freq), octaves, f) != 0) {
      return -1;
    }
  }

  /* P:0x090620 */
  coef[0] = gm2_swf_to_q23(gm2_swf_mul(f[1], gm2_swf_quarter));
  coef[1] = gm2_swf_to_q23(gm2_swf_mul(f[2], gm2_swf_quarter));
  coef[2] = gm2_swf_to_q23(gm2_swf_mul(f[0], gm2_swf_quarter));
  coef[3] =
      gm2_swf_to_q23(gm2_swf_mul(gm2_swf_sub(gm2_swf_neg(f[3]), gm2_swf_one), gm2_swf_quarter));
  coef[4] = gm2_swf_to_q23(gm2_swf_mul(gm2_swf_neg(f[4]), gm2_swf_half));
  return 0;
}

int gm2_eq_setup_config(const gm2_context *context, const gm2_eq_setup_raw *raw,
                        gm2_eq_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  for (unsigned band = 0; band < GM2_EQ_BANDS; band++) {
    if (eq_band(&rate_words, raw->band[band].freq, raw->band[band].gain, raw->band[band].width,
                &config->coef[GM2_BIQUAD_COEFS * band]) != 0) {
      return -1;
    }
  }
  return 0;
}
