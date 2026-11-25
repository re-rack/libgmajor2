/* Ping Pong Delay setup, Delay block subtype 0: the twenty coefficient words
 * X+0..19 and both tap offsets, from Delay, Tempo, Width, Feedback, Feedback
 * Hi-Cut and Feedback Lo-Cut, all published by the block's activation P:0x09423F.
 */
#include "gm2_pingpong_setup.h"

#include "gm2_delay_design.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  DELAY_MIN = 0,
  DELAY_MAX = 1800,
};

int gm2_pingpong_setup_config(const gm2_context *context, const gm2_pingpong_setup_raw *raw,
                              gm2_pingpong_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  gm2_delay_design_matrix(GM2_DELAY_DESIGN_PINGPONG, raw->feedback, 0,
                          &config->coef[GM2_DELAY_CORE_INPUT]);
  gm2_delay_design_output(GM2_DELAY_DESIGN_PINGPONG, raw->width, 0, 0,
                          &config->coef[GM2_DELAY_CORE_OUTPUT]);
  gm2_delay_design_filters(&rate_words, GM2_DELAY_DESIGN_PINGPONG, raw->fb_hi_cut, raw->fb_lo_cut,
                           &config->coef[GM2_DELAY_CORE_FILTER]);

  /* Both lines run at the one delay time. */
  const int32_t ms =
      gm2_delay_design_ms(raw->delay, DELAY_MIN, DELAY_MAX, raw->tempo, raw->beat_ms);
  for (unsigned line = 0; line < GM2_DELAY_CORE_LINES; line++) {
    config->tap_offset[line] = gm2_delay_design_tap_offset(&rate_words, ms, line);
  }
  return 0;
}
