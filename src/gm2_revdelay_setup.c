/* Reverse Delay setup, Delay block subtype 3: the twenty coefficient words
 * X+0..19, the Mix pair X:0x098ED0/X:0x098ED1, the grain length in milliseconds
 * and the grain period in four-frame chunks, all published by P:0x09423F.
 */
#include "gm2_revdelay_setup.h"

#include "gm2_delay_design.h"
#include "gm2_mix.h"
#include "gm2_tick.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  DELAY_MIN = 50,
  DELAY_MAX = 1000,
};

int gm2_revdelay_setup_config(const gm2_context *context, const gm2_revdelay_setup_raw *raw,
                              gm2_revdelay_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  gm2_mix_words(raw->mix, &config->wet, &config->dry);

  gm2_delay_design_matrix(GM2_DELAY_DESIGN_REVDELAY, raw->feedback, 0,
                          &config->coef[GM2_DELAY_CORE_INPUT]);
  gm2_delay_design_output(GM2_DELAY_DESIGN_REVDELAY, 0, 0, 0, config->wet,
                          &config->coef[GM2_DELAY_CORE_OUTPUT]);
  gm2_delay_design_filters(&rate_words, GM2_DELAY_DESIGN_REVDELAY, raw->fb_hi_cut, raw->fb_lo_cut,
                           &config->coef[GM2_DELAY_CORE_FILTER]);

  config->delay_ms =
      gm2_delay_design_ms(raw->delay, DELAY_MIN, DELAY_MAX, raw->tempo, raw->beat_ms);
  config->grain_chunks = gm2_tick_period(rate, (unsigned)config->delay_ms);
  return 0;
}
