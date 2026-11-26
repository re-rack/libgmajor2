/* The Delay block's setup routines P:0x094574, P:0x094643, P:0x094835,
 * P:0x094735 and P:0x09479E, on the firmware's software float.
 *
 * The values are pairs (mantissa, exponent) with value = mantissa * 2^(exponent -
 * 150); the arithmetic steps are the ports in gm2_swf.c, the sample-rate words
 * those of gm2_rate.c and the Tempo factors those of gm2_tables.c. With FB1, FB2
 * the Feedbacks, W the Width, P1, P2 the Pans, H, L the Hi-Cut and Lo-Cut, D1, D2
 * the Delay times, T1, T2 the Tempo values and B the host's beat in milliseconds,
 * the routines are
 *
 *   P:0x094574  X+0..7:   fb = to_q23(FB * {0x51EB85,0x79}),
 *               subtype 0: {0x400000, 0, 0x400000, 0, 0, 0, fb1, fb1}
 *               subtype 1: {0x400000, 0, 0x400000, 0, 0, fb1, 0x7FFFFF, 0}
 *               subtype 2, 3: {0x7FFFFE, 0, 0, 0x7FFFFE, fb2, fb1, 0, 0}
 *   P:0x094643  X+8..11:  subtype 0: a = to_q23((0.5 + W * {0x51EB85,0x78}) S),
 *                         b = to_q23((0.5 - W * {0x51EB85,0x78}) S), {a, a, b, b},
 *                         with S = from_q23(X+51), the Mix wet word
 *               subtype 1: {0, 0, 0, 0}; the caller stages the ducking gain in
 *                         X+8 and X+9
 *               subtype 2, 3: r = to_q23((Pan + 50) * {0x51EB85,0x79}),
 *                         {mpyr(r2, S), mpyr(0x7FFFFF - r1, S),
 *                          mpyr(r1, S), mpyr(0x7FFFFF - r2, S)}
 *   P:0x094835  X+12..19: t = tan(pi * Hz(cut) / rate),
 *               c = to_q23((t - 1) / (t + 1)), d = macr(0.5, +-c, 0.5),
 *               {c_hi, d_hi, c_lo, d_lo}; X+16..19 are
 *               {0xF79C46, 0x3BCE23, 0x802027, 0x7FEFEC} on subtype 1 and a copy
 *               of X+12..15 otherwise
 *   P:0x094735  ms = min(1800, to_int(b * note[T])) when T is not Ignore, else the
 *               clamped D, and the truncated ms is written back over the raw slot
 *   P:0x09479E  samples = to_int({0x400008,0x7F} + ms * rate_khz),
 *               tap = 2 * samples + 1 for line 0 and + 2 for line 1
 */
#include "gm2_delay_design.h"

#include "gm2_dsp_math.h"
#include "gm2_lfo.h"
#include "gm2_swf.h"
#include "gm2_tables.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  FEEDBACK_MIN = 0,
  FEEDBACK_MAX = 100,
  WIDTH_MIN = 0,
  WIDTH_MAX = 100,
  PAN_MIN = -50,
  PAN_MAX = 50,
  FB_HI_CUT_MIN = 40,
  FB_HI_CUT_MAX = 60,
  FB_LO_CUT_MIN = 0,
  FB_LO_CUT_MAX = 40,
  TEMPO_MIN = 0,
  TEMPO_MAX = GM2_LFO_NOTE_MAX,
};

/* The longest delay P:0x094735 passes on, in ms. */
enum { DELAY_DESIGN_DELAY_MS_MAX = 1800 };

/* The firmware's operands as (mantissa, exponent) words. */
static const gm2_swf k_semi = GM2_SWF_CONST(0x51EB85u, 0x78u); /* 0.005 */
static const gm2_swf k_bias = GM2_SWF_CONST(0x400008u, 0x7Fu); /* 0.500000477 */

/* The subtype-1 filter words P:0x094835 leaves in X+16..19. */
static const int32_t k_dynamic_filter[4] = {-0x0863BA, 0x3BCE23, -0x7FDFD9, 0x7FEFEC};

void gm2_delay_design_matrix(gm2_delay_design_subtype subtype, int feedback_1, int feedback_2,
                             int32_t coef[8]) {
  const int32_t fb1 = gm2_swf_to_q23(gm2_swf_mul(
      gm2_swf_from_int(gm2_clamp_raw(feedback_1, FEEDBACK_MIN, FEEDBACK_MAX)), gm2_swf_hundredth));
  if (subtype == GM2_DELAY_DESIGN_PINGPONG) {
    const int32_t words0[8] = {0x400000, 0, 0x400000, 0, 0, 0, fb1, fb1};
    for (unsigned i = 0; i < 8u; i++) {
      coef[i] = words0[i];
    }
  } else if (subtype == GM2_DELAY_DESIGN_DYNDELAY) {
    const int32_t words1[8] = {0x400000, 0, 0x400000, 0, 0, fb1, 0x7FFFFF, 0};
    for (unsigned i = 0; i < 8u; i++) {
      coef[i] = words1[i];
    }
  } else {
    const int32_t fb2 = gm2_swf_to_q23(
        gm2_swf_mul(gm2_swf_from_int(gm2_clamp_raw(feedback_2, FEEDBACK_MIN, FEEDBACK_MAX)),
                    gm2_swf_hundredth));
    const int32_t words23[8] = {0x7FFFFE, 0, 0, 0x7FFFFE, fb2, fb1, 0, 0};
    for (unsigned i = 0; i < 8u; i++) {
      coef[i] = words23[i];
    }
  }
}

void gm2_delay_design_output(gm2_delay_design_subtype subtype, int width, int pan_1, int pan_2,
                             int32_t wet, int32_t coef[4]) {
  if (subtype == GM2_DELAY_DESIGN_PINGPONG) {
    const gm2_swf scale = gm2_swf_from_q23(wet);
    const gm2_swf width_value = gm2_swf_from_int(gm2_clamp_raw(width, WIDTH_MIN, WIDTH_MAX));
    const int32_t a =
        gm2_swf_to_q23(gm2_swf_mul(gm2_swf_mac(gm2_swf_half, width_value, k_semi), scale));
    const int32_t b =
        gm2_swf_to_q23(gm2_swf_mul(gm2_swf_msub(gm2_swf_half, width_value, k_semi), scale));
    coef[0] = a;
    coef[1] = a;
    coef[2] = b;
    coef[3] = b;
  } else if (subtype == GM2_DELAY_DESIGN_DYNDELAY) {
    for (unsigned i = 0; i < 4u; i++) {
      coef[i] = 0;
    }
  } else {
    const gm2_swf r1 = gm2_swf_mul(gm2_swf_from_int(gm2_clamp_raw(pan_1, PAN_MIN, PAN_MAX) + 50),
                                   gm2_swf_hundredth);
    const gm2_swf r2 = gm2_swf_mul(gm2_swf_from_int(gm2_clamp_raw(pan_2, PAN_MIN, PAN_MAX) + 50),
                                   gm2_swf_hundredth);
    const int32_t w1 = gm2_swf_to_q23(r1);
    const int32_t w2 = gm2_swf_to_q23(r2);
    coef[0] = gm2_store24(gm2_mpyr(w2, wet));
    coef[1] = gm2_store24(gm2_mpyr(0x7FFFFF - w1, wet));
    coef[2] = gm2_store24(gm2_mpyr(w1, wet));
    coef[3] = gm2_store24(gm2_mpyr(0x7FFFFF - w2, wet));
  }
}

void gm2_delay_design_filters(const gm2_rate_words *rate, gm2_delay_design_subtype subtype,
                              int fb_hi_cut, int fb_lo_cut, int32_t coef[8]) {
  const gm2_swf f_lo = gm2_swf_hz20(gm2_clamp_raw(fb_lo_cut, FB_LO_CUT_MIN, FB_LO_CUT_MAX));
  const gm2_swf f_hi = gm2_swf_hz20(gm2_clamp_raw(fb_hi_cut, FB_HI_CUT_MIN, FB_HI_CUT_MAX));
  const gm2_swf one = gm2_swf_one;

  const gm2_swf t_hi = gm2_swf_tan(gm2_swf_div(gm2_swf_mul(f_hi, gm2_swf_pi), rate->rate));
  const int32_t c12 = gm2_swf_to_q23(gm2_swf_div(gm2_swf_sub(t_hi, one), gm2_swf_add(t_hi, one)));
  const int32_t c13 = gm2_store24(gm2_macr(gm2_word_acc(0x400000), c12, 0x400000));

  const gm2_swf t_lo = gm2_swf_tan(gm2_swf_div(gm2_swf_mul(f_lo, gm2_swf_pi), rate->rate));
  const int32_t c14 = gm2_swf_to_q23(gm2_swf_div(gm2_swf_sub(t_lo, one), gm2_swf_add(one, t_lo)));
  const int32_t c15 = gm2_store24(gm2_macr(gm2_word_acc(0x400000), -c14, 0x400000));

  coef[0] = c12;
  coef[1] = c13;
  coef[2] = c14;
  coef[3] = c15;
  if (subtype == GM2_DELAY_DESIGN_DYNDELAY) {
    for (unsigned i = 0; i < 4u; i++) {
      coef[4u + i] = k_dynamic_filter[i];
    }
  } else {
    for (unsigned i = 0; i < 4u; i++) {
      coef[4u + i] = coef[i];
    }
  }
}

int32_t gm2_delay_design_ms(int delay, int delay_min, int delay_max, int tempo, double beat_ms) {
  const int note = gm2_clamp_raw(tempo, TEMPO_MIN, TEMPO_MAX);
  int32_t ms;
  if (note != 0) {
    ms = gm2_swf_to_int(gm2_swf_mul(gm2_lfo_beat(beat_ms), gm2_delay_note_table[note]));
  } else {
    ms = (int32_t)gm2_clamp_raw(delay, delay_min, delay_max);
  }
  return ms > DELAY_DESIGN_DELAY_MS_MAX ? DELAY_DESIGN_DELAY_MS_MAX : ms;
}

int32_t gm2_delay_design_tap_offset(const gm2_rate_words *rate, int32_t delay_ms, unsigned line) {
  const int32_t samples =
      gm2_swf_to_int(gm2_swf_mac(k_bias, gm2_swf_from_int(delay_ms), rate->rate_khz));
  return 2 * samples + (line == 0u ? 1 : 2);
}
