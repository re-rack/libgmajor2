/* The Panner's fixed words on top of the shared updaters
 * (gm2_lfo_gain_design.h). */
#include "gm2_panner_setup.h"

#include "gm2_dsp_math.h"
#include "gm2_lfo_gain_design.h"
#include "gm2_rate.h"

/* Y:0x3B6 as P:0x0919C4 stages it for subtype 6: half a cycle. */
enum { PANNER_LAG = 0x400000 };

int gm2_panner_setup_config(const gm2_context *context, const gm2_panner_setup_raw *raw,
                            gm2_panner_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  const gm2_lfo_gain_design_raw design = {
      .speed = raw->speed,
      .depth = raw->width,
      .tempo = raw->tempo,
      .beat_ms = raw->beat_ms,
      .out_level = raw->out_level,
  };
  gm2_lfo_gain_design(&rate_words, &design, config);

  /* The sine reads none of the three pulse words, and the Panner has no Pulse
   * Width. */
  config->coef[GM2_LFO_GAIN_CORE_PULSE] = 0;
  config->coef[GM2_LFO_GAIN_CORE_RISE] = 0;
  config->coef[GM2_LFO_GAIN_CORE_FALL] = 0;
  config->raw[GM2_LFO_GAIN_CORE_RAW_PULSE_WIDTH] = 0;

  config->wave = GM2_LFO_GAIN_CORE_WAVE_SINE;
  config->lag = PANNER_LAG;

  /* P:0x091876 leaves the pole at 0: the Hi-Cut passes everything. */
  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    config->coef[GM2_LFO_GAIN_CORE_HI_CUT + 2u * channel] = 0;
    config->coef[GM2_LFO_GAIN_CORE_HI_CUT + 2u * channel + 1u] = GM2_W24_MAX;
  }
  return 0;
}
