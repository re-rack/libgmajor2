/* The Phaser block's coefficient design, P:0x092B92 calling P:0x092D6D,
 * P:0x092D1C, P:0x092E88, P:0x092EAF, P:0x092EDA, P:0x092E54 and P:0x092F2B, all
 * on the firmware's software float.
 *
 * With S the Speed value, D Depth, T Tempo, F Feedback, R Range and P the
 * phase-reverse switch, the words are
 *
 *   P:0x092D6D  nine Range-table words through ((-(1 - x))/2)^(1/3) at the
 *               Low (index 9) or High (index 0) block of X:0x098ACA:
 *               S+3, S+7, S+5 = the first of each pair, S+2, S+6, S+4 = the
 *               differences of the pairs, S+11, S+12, S+13 = the last three
 *   P:0x092D1C  e = exp({0xA7A489, 0x8C} / rate),
 *               S+8 = (1 - e) ({0x425076, 0x80} - {0x8A1BA0, 0x7C}) / 4,
 *               S+9 = e / 4, S+10 = {0x8A1BA0, 0x7C} / 4
 *   P:0x092E88  feedback = F {0x4C2F83, 0x79}
 *   P:0x092EAF  depth = min(D {0x51EB85, 0x79}, 0x7FBE77)
 *   P:0x092EDA  step = the phase step of Speed S or Tempo T (gm2_lfo.h)
 *   P:0x092E54  the tap weights: the four-stage taps 0x7FFFF8 for Vintage
 *               (subtype 9), the twelve-stage taps for Smooth (subtype 10)
 *   P:0x092F2B  right gain = 0x7FFFFF, or 0x800001 with the phase reversed
 *
 * Mix is not one of them: the level code P:0x08F763 stages the pair the output
 * stage multiplies each chain's output and the frame's input by, at Out Level
 * 0 dB.
 */
#include "gm2_phaser_design.h"

#include "gm2_dsp_math.h"
#include "gm2_lfo.h"
#include "gm2_mix.h"
#include "gm2_swf.h"
#include "gm2_tables.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  SPEED_MIN = 0,
  SPEED_MAX = GM2_LFO_SPEED_MAX,
  DEPTH_MIN = 0,
  DEPTH_MAX = 100,
  TEMPO_MIN = 0,
  TEMPO_MAX = GM2_LFO_NOTE_MAX,
  FEEDBACK_MIN = -100,
  FEEDBACK_MAX = 100,
  RANGE_MIN = 0,
  RANGE_MAX = 1,
  PHASE_REVERSE_MIN = 0,
  PHASE_REVERSE_MAX = 1,
};

/* The tap weight both subtypes stage at 0x7FFFF8, the deepest Depth word and the
 * phase-reverse sign. */
enum {
  PHASER_DESIGN_TAP_WEIGHT = 0x7FFFF8,
  PHASER_DESIGN_DEPTH_WORD_MAX = 0x7FBE77,
  PHASER_DESIGN_PHASE_NORMAL = 0x7FFFFF,
  PHASER_DESIGN_PHASE_REVERSED = -0x7FFFFF,
};

static const gm2_swf k_third = GM2_SWF_CONST(0x555555u, 0x7Eu);           /* 1/3 */
static const gm2_swf k_low_pass_rate = GM2_SWF_CONST(0xA7A489u, 0x8Cu);   /* -5654.866211 */
static const gm2_swf k_low_pass_top = GM2_SWF_CONST(0x425076u, 0x80u);    /* 1.036161 */
static const gm2_swf k_low_pass_bottom = GM2_SWF_CONST(0x8A1BA0u, 0x7Cu); /* -0.115129 */
static const gm2_swf k_feedback_step = GM2_SWF_CONST(0x4C2F83u, 0x79u);   /* 0.0093 */
static const gm2_swf k_percent = GM2_SWF_CONST(0x51EB85u, 0x79u);         /* 0.01 */

/* P:0x092D04: the cube root the Range table's words go through. */
static gm2_swf phaser_curve(gm2_swf x) {
  return gm2_swf_pow(gm2_swf_div(gm2_swf_sub(gm2_swf_one, x), gm2_swf_two), k_third);
}

void gm2_phaser_design(const gm2_rate_words *rate, gm2_phaser_design_subtype subtype,
                       const gm2_phaser_design_raw *raw, gm2_phaser_core_config *config) {
  const int speed = gm2_clamp_raw(raw->speed, SPEED_MIN, SPEED_MAX);
  const int depth = gm2_clamp_raw(raw->depth, DEPTH_MIN, DEPTH_MAX);
  const int tempo = gm2_clamp_raw(raw->tempo, TEMPO_MIN, TEMPO_MAX);
  const int feedback = gm2_clamp_raw(raw->feedback, FEEDBACK_MIN, FEEDBACK_MAX);
  const int range = gm2_clamp_raw(raw->range, RANGE_MIN, RANGE_MAX);
  const int phase_reverse = gm2_clamp_raw(raw->phase_reverse, PHASE_REVERSE_MIN, PHASE_REVERSE_MAX);

  /* The Range table's Low block follows its High block, so Low starts at index 9. */
  const unsigned base = range == 0 ? 9u : 0u;
  gm2_swf curve[9];
  for (unsigned i = 0; i < 9u; i++) {
    curve[i] = phaser_curve(gm2_phaser_range_table[base + i]);
  }
  config->coef[GM2_PHASER_CORE_SWEEP + 1u] = gm2_swf_to_q23(curve[0]);
  config->coef[GM2_PHASER_CORE_SWEEP + 5u] = gm2_swf_to_q23(curve[2]);
  config->coef[GM2_PHASER_CORE_SWEEP + 3u] = gm2_swf_to_q23(curve[4]);
  config->coef[GM2_PHASER_CORE_SWEEP] = gm2_swf_to_q23(gm2_swf_sub(curve[1], curve[0]));
  config->coef[GM2_PHASER_CORE_SWEEP + 4u] = gm2_swf_to_q23(gm2_swf_sub(curve[3], curve[2]));
  config->coef[GM2_PHASER_CORE_SWEEP + 2u] = gm2_swf_to_q23(gm2_swf_sub(curve[5], curve[4]));
  config->coef[GM2_PHASER_CORE_POLE] = gm2_swf_to_q23(curve[6]);
  config->coef[GM2_PHASER_CORE_POLE + 1u] = gm2_swf_to_q23(curve[7]);
  config->coef[GM2_PHASER_CORE_POLE + 2u] = gm2_swf_to_q23(curve[8]);

  /* The feedback path's low-pass: one pole from the sample period against the
   * two fixed poles Y:0x0445 and Y:0x0447 the kernel keeps. */
  const gm2_swf e0 = gm2_swf_exp(gm2_swf_mul(rate->period, k_low_pass_rate));
  const gm2_swf e1 = gm2_swf_exp(k_low_pass_top);
  const gm2_swf e2 = gm2_swf_exp(k_low_pass_bottom);
  config->coef[GM2_PHASER_CORE_LOW_PASS] = gm2_swf_to_q23(
      gm2_swf_mul(gm2_swf_mul(gm2_swf_sub(gm2_swf_one, e0), gm2_swf_sub(e1, e2)), gm2_swf_quarter));
  config->coef[GM2_PHASER_CORE_LOW_PASS + 1u] = gm2_swf_to_q23(gm2_swf_mul(e0, gm2_swf_quarter));
  config->coef[GM2_PHASER_CORE_LOW_PASS + 2u] = gm2_swf_to_q23(gm2_swf_mul(e2, gm2_swf_quarter));

  config->coef[GM2_PHASER_CORE_FEEDBACK] =
      gm2_swf_to_q23(gm2_swf_mul(gm2_swf_from_int(feedback), k_feedback_step));

  const int32_t depth_word = gm2_swf_to_q23(gm2_swf_mul(gm2_swf_from_int(depth), k_percent));
  config->coef[GM2_PHASER_CORE_DEPTH] =
      depth_word > PHASER_DESIGN_DEPTH_WORD_MAX ? PHASER_DESIGN_DEPTH_WORD_MAX : depth_word;

  config->coef[GM2_PHASER_CORE_STEP] =
      gm2_lfo_step(rate, gm2_lfo_rate(speed, (unsigned)tempo, raw->beat_ms));

  const int32_t low_tap = subtype == GM2_PHASER_DESIGN_SMOOTH ? 0 : PHASER_DESIGN_TAP_WEIGHT;
  const int32_t high_tap = subtype == GM2_PHASER_DESIGN_SMOOTH ? PHASER_DESIGN_TAP_WEIGHT : 0;
  config->coef[GM2_PHASER_CORE_TAP_LEFT] = low_tap;
  config->coef[GM2_PHASER_CORE_TAP_LEFT + 1u] = high_tap;
  config->coef[GM2_PHASER_CORE_TAP_RIGHT] = low_tap;
  config->coef[GM2_PHASER_CORE_TAP_RIGHT + 1u] = high_tap;

  config->coef[GM2_PHASER_CORE_RIGHT_GAIN] =
      phase_reverse != 0 ? PHASER_DESIGN_PHASE_REVERSED : PHASER_DESIGN_PHASE_NORMAL;
  config->raw[GM2_PHASER_CORE_RAW_SPEED] = speed;
  config->raw[GM2_PHASER_CORE_RAW_DEPTH] = depth;
  config->raw[GM2_PHASER_CORE_RAW_TEMPO] = tempo;
  config->note = (unsigned)tempo;
  gm2_mix_words(raw->mix, &config->wet, &config->dry);
}
