/* The Compressor's two updaters on the firmware's software float. With T, G the
 * raw Threshold and Gain, R = gm2_ratio_table[Ratio], tA, tR the Attack and
 * Release times in seconds and db = {0x550A96,0x77}, log2(gain)/64 per dB:
 *
 *   P:0x094D5C  threshold = (T - 3) * db
 *               ratio     = 1/R - 1
 *               gain      = (G + K) * db, K = {0x879687,0x84} (about -30.1 dB),
 *                           plus (-20 - T) * (1 - 1/R) while T is below -20
 *   P:0x094E18  step      = {0x457CAF,0x83} / tR * db / (rate/4)
 *               detector  = 1 - exp(-1 / (tA * rate))
 *               attack    = {(fast - slow) / {0x710020,0x70}, slow, fast},
 *                           fast = exp(-1 / (rate/4 * tA)),
 *                           slow = exp(-1 / (1000 * rate/4 * tA))
 *               release   = {(fast - slow) / {0x584819,0x75}, slow, fast},
 *                           fast = exp(-1 / (0.3 * rate/4 * tR)),
 *                           slow = exp(-1 / (100 * rate/4 * tR))
 *
 * The two blend divisors are so small that the blend words sit at the negative
 * rail for every setting.
 */
#include "gm2_comp_setup.h"

#include "gm2_dsp_math.h"
#include "gm2_rate.h"
#include "gm2_swf.h"
#include "gm2_tables.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  THRESHOLD_MIN = -40,
  THRESHOLD_MAX = 0,
  RATIO_MIN = 0,
  RATIO_MAX = (int)GM2_RATIO_TABLE_ENTRIES - 1,
  ATTACK_MIN = 3,
  ATTACK_MAX = 14,
  RELEASE_MIN = 13,
  RELEASE_MAX = 23,
  GAIN_MIN = -6,
  GAIN_MAX = 6,
};

static const gm2_swf k_db = GM2_SWF_CONST(0x550A96u, 0x77u);            /* log2(gain)/64 per dB */
static const gm2_swf k_three = GM2_SWF_CONST(0x600000u, 0x81u);         /* 3 */
static const gm2_swf k_headroom = GM2_SWF_CONST(0x879687u, 0x84u);      /* -30.1 */
static const gm2_swf k_makeup_from = GM2_SWF_CONST(0xB00000u, 0x84u);   /* -20 */
static const gm2_swf k_step_scale = GM2_SWF_CONST(0x457CAFu, 0x83u);    /* 8.69 */
static const gm2_swf k_attack_slow = GM2_SWF_CONST(0x7D0000u, 0x89u);   /* 1000 */
static const gm2_swf k_attack_blend = GM2_SWF_CONST(0x710020u, 0x70u);  /* 0.000027 */
static const gm2_swf k_release_fast = GM2_SWF_CONST(0x4CCCCDu, 0x7Eu);  /* 0.3 */
static const gm2_swf k_release_slow = GM2_SWF_CONST(0x640000u, 0x86u);  /* 100 */
static const gm2_swf k_release_blend = GM2_SWF_CONST(0x584819u, 0x75u); /* 0.00067 */

/* exp(-1 / (scale * time)): a one-pole's coefficient for a time in units of
 * 1/scale. */
static gm2_swf comp_pole(gm2_swf scale, gm2_swf time) {
  return gm2_swf_exp(gm2_swf_div(gm2_swf_minus_one, gm2_swf_mul(scale, time)));
}

/* P:0x094E18: the eight words Attack and Release stage. */
static void comp_times(const gm2_rate_words *rate, int attack, int release, int32_t *coef) {
  const gm2_swf chunk_rate = gm2_swf_div(rate->rate, gm2_swf_four);
  const gm2_swf attack_time = gm2_swf_mul(gm2_time_table[attack], gm2_swf_thousandth);
  const gm2_swf release_time = gm2_swf_mul(gm2_time_table[release], gm2_swf_thousandth);

  const gm2_swf attack_fast = comp_pole(chunk_rate, attack_time);
  const gm2_swf attack_slow = comp_pole(gm2_swf_mul(k_attack_slow, chunk_rate), attack_time);
  const gm2_swf release_fast = comp_pole(gm2_swf_mul(k_release_fast, chunk_rate), release_time);
  const gm2_swf release_slow = comp_pole(gm2_swf_mul(k_release_slow, chunk_rate), release_time);

  coef[GM2_COMP_DETECTOR] =
      gm2_swf_to_q23(gm2_swf_sub(gm2_swf_one, comp_pole(rate->rate, attack_time)));
  coef[GM2_COMP_RELEASE_STEP] = gm2_swf_to_q23(
      gm2_swf_div(gm2_swf_mul(gm2_swf_div(k_step_scale, release_time), k_db), chunk_rate));
  coef[GM2_COMP_RELEASE] =
      gm2_swf_to_q23(gm2_swf_div(gm2_swf_sub(release_fast, release_slow), k_release_blend));
  coef[GM2_COMP_RELEASE + 1] = gm2_swf_to_q23(release_slow);
  coef[GM2_COMP_RELEASE + 2] = gm2_swf_to_q23(release_fast);
  coef[GM2_COMP_ATTACK] =
      gm2_swf_to_q23(gm2_swf_div(gm2_swf_sub(attack_fast, attack_slow), k_attack_blend));
  coef[GM2_COMP_ATTACK + 1] = gm2_swf_to_q23(attack_slow);
  coef[GM2_COMP_ATTACK + 2] = gm2_swf_to_q23(attack_fast);
}

int gm2_comp_setup_config(const gm2_context *context, const gm2_comp_setup_raw *raw,
                          gm2_comp_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }
  const int threshold = gm2_clamp_raw(raw->threshold, THRESHOLD_MIN, THRESHOLD_MAX);
  const int ratio = gm2_clamp_raw(raw->ratio, RATIO_MIN, RATIO_MAX);
  const int attack = gm2_clamp_raw(raw->attack, ATTACK_MIN, ATTACK_MAX);
  const int release = gm2_clamp_raw(raw->release, RELEASE_MIN, RELEASE_MAX);
  const int gain = gm2_clamp_raw(raw->gain, GAIN_MIN, GAIN_MAX);

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  /* P:0x094D5C */
  const gm2_swf level = gm2_swf_from_int(threshold);
  const gm2_swf slope = gm2_swf_div(gm2_swf_one, gm2_ratio_table[ratio]);
  gm2_swf makeup = k_headroom;
  if (gm2_swf_cmp(k_makeup_from, level) > 0) {
    makeup =
        gm2_swf_mac(k_headroom, gm2_swf_sub(k_makeup_from, level), gm2_swf_sub(gm2_swf_one, slope));
  }
  config->coef[GM2_COMP_THRESHOLD] = gm2_swf_to_q23(gm2_swf_mul(gm2_swf_sub(level, k_three), k_db));
  config->coef[GM2_COMP_RATIO] = gm2_swf_to_q23(gm2_swf_sub(slope, gm2_swf_one));
  config->coef[GM2_COMP_GAIN] =
      gm2_swf_to_q23(gm2_swf_mul(gm2_swf_add(gm2_swf_from_int(gain), makeup), k_db));

  comp_times(&rate_words, attack, release, config->coef);

  config->raw[GM2_COMP_THRESHOLD] = threshold;
  config->raw[GM2_COMP_RATIO] = ratio;
  config->raw[GM2_COMP_GAIN] = gain;
  return 0;
}
