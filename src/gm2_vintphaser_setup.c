/* The Vintage Phaser's words from the shared phaser design
 * (gm2_phaser_design.h). */
#include "gm2_vintphaser_setup.h"

#include "gm2_phaser_design.h"
#include "gm2_rate.h"

int gm2_vintphaser_setup_config(const gm2_context *context, const gm2_vintphaser_setup_raw *raw,
                                gm2_vintphaser_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  const gm2_phaser_design_raw design = {
      .speed = raw->speed,
      .depth = raw->depth,
      .tempo = raw->tempo,
      .feedback = raw->feedback,
      .range = raw->range,
      .phase_reverse = raw->phase_reverse,
      .mix = raw->mix,
      .out_level = raw->out_level,
      .beat_ms = raw->beat_ms,
  };
  gm2_phaser_design(&rate_words, GM2_PHASER_DESIGN_VINTAGE, &design, config);
  return 0;
}
