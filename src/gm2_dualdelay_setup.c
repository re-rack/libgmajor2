/* Dual Delay setup, Delay block subtype 2: the twenty coefficient words X+0..19
 * and both tap offsets, from Delay 1, Delay 2, Tempo 1, Tempo 2, Feedback 1,
 * Feedback 2, Feedback Hi-Cut, Feedback Lo-Cut, Pan 1 and Pan 2, all published by
 * P:0x09423F.
 */
#include "gm2_dualdelay_setup.h"

#include "gm2_delay_design.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  DELAY_MIN = 0,
  DELAY_MAX = 1800,
};

int gm2_dualdelay_setup_config(const gm2_context *context, const gm2_dualdelay_setup_raw *raw,
                               gm2_dualdelay_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  gm2_delay_design_matrix(GM2_DELAY_DESIGN_DUALDELAY, raw->feedback_1, raw->feedback_2,
                          &config->coef[GM2_DELAY_CORE_INPUT]);
  gm2_delay_design_output(GM2_DELAY_DESIGN_DUALDELAY, 0, raw->pan_1, raw->pan_2,
                          &config->coef[GM2_DELAY_CORE_OUTPUT]);
  gm2_delay_design_filters(&rate_words, GM2_DELAY_DESIGN_DUALDELAY, raw->fb_hi_cut, raw->fb_lo_cut,
                           &config->coef[GM2_DELAY_CORE_FILTER]);

  const int32_t ms1 =
      gm2_delay_design_ms(raw->delay_1, DELAY_MIN, DELAY_MAX, raw->tempo_1, raw->beat_ms);
  const int32_t ms2 =
      gm2_delay_design_ms(raw->delay_2, DELAY_MIN, DELAY_MAX, raw->tempo_2, raw->beat_ms);
  config->tap_offset[0] = gm2_delay_design_tap_offset(&rate_words, ms1, 0u);
  config->tap_offset[1] = gm2_delay_design_tap_offset(&rate_words, ms2, 1u);
  return 0;
}
