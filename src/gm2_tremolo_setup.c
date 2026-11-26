/* The Tremolo's own updaters on the firmware's software float, on top of the
 * shared ones (gm2_lfo_gain_design.h). With P = 10 * Pulse Width held in 1..999:
 *
 *   P:0x0918CE  pulse  = P * 0.001
 *               rise   = 2^-10 / pulse
 *               fall   = 2^-10 / (pulse - 1)
 *   P:0x091876  hi-cut = {pole, 1 - pole}, pole = exp(-2 pi Hz(Hi-Cut) / rate)
 */
#include "gm2_tremolo_setup.h"

#include "gm2_dsp_math.h"
#include "gm2_lfo_gain_design.h"
#include "gm2_rate.h"
#include "gm2_swf.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  TYPE_MIN = 0,
  TYPE_MAX = 1,
  PULSE_WIDTH_MIN = 0,
  PULSE_WIDTH_MAX = 100,
  HI_CUT_MIN = 0,
  HI_CUT_MAX = 60,
};

/* The thousandths of a cycle P:0x0918CE holds the pulse word in. */
enum {
  PULSE_MIN = 1,
  PULSE_MAX = 999,
};

static const gm2_swf k_slope = GM2_SWF_CONST(0x400000u, 0x76u);      /* 2^-10 */

int gm2_tremolo_setup_config(const gm2_context *context, const gm2_tremolo_setup_raw *raw,
                             gm2_tremolo_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }
  const int type = gm2_clamp_raw(raw->type, TYPE_MIN, TYPE_MAX);
  const int pulse_width = gm2_clamp_raw(raw->pulse_width, PULSE_WIDTH_MIN, PULSE_WIDTH_MAX);
  const int hi_cut = gm2_clamp_raw(raw->hi_cut, HI_CUT_MIN, HI_CUT_MAX);

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  const gm2_lfo_gain_design_raw design = {
      .speed = raw->speed,
      .depth = raw->depth,
      .tempo = raw->tempo,
      .beat_ms = raw->beat_ms,
      .out_level = raw->out_level,
  };
  gm2_lfo_gain_design(&rate_words, &design, config);

  int thousandths = 10 * pulse_width;
  if (thousandths < PULSE_MIN) {
    thousandths = PULSE_MIN;
  } else if (thousandths > PULSE_MAX) {
    thousandths = PULSE_MAX;
  }
  const gm2_swf pulse = gm2_swf_mul(gm2_swf_from_int(thousandths), gm2_swf_thousandth);
  config->coef[GM2_LFO_GAIN_CORE_PULSE] = gm2_swf_to_q23(pulse);
  config->coef[GM2_LFO_GAIN_CORE_RISE] = gm2_swf_to_q23(gm2_swf_div(k_slope, pulse));
  config->coef[GM2_LFO_GAIN_CORE_FALL] =
      gm2_swf_to_q23(gm2_swf_div(k_slope, gm2_swf_sub(pulse, gm2_swf_one)));
  config->raw[GM2_LFO_GAIN_CORE_RAW_PULSE_WIDTH] = pulse_width;

  config->wave = type != 0 ? GM2_LFO_GAIN_CORE_WAVE_HARD : GM2_LFO_GAIN_CORE_WAVE_SOFT;
  config->lag = 0;

  const int32_t pole =
      gm2_swf_to_q23(gm2_swf_exp(gm2_swf_mul(gm2_swf_hz20(hi_cut), rate_words.minus_omega)));
  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    config->coef[GM2_LFO_GAIN_CORE_HI_CUT + 2u * channel] = pole;
    config->coef[GM2_LFO_GAIN_CORE_HI_CUT + 2u * channel + 1u] = GM2_W24_MAX - pole;
  }
  return 0;
}
