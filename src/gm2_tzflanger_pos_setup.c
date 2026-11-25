#include "gm2_tzflanger_pos_setup.h"

#include "gm2_moddelay_design.h"
#include "gm2_rate.h"

int gm2_tzflanger_pos_setup_config(const gm2_context *context,
                                   const gm2_tzflanger_pos_setup_raw *raw,
                                   gm2_tzflanger_pos_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  const gm2_moddelay_design_raw design = {
      .speed = raw->speed,
      .depth = raw->depth,
      .tempo = raw->tempo,
      .feedback = raw->feedback,
      .fb_cut = raw->fb_cut,
      .beat_ms = raw->beat_ms,
      .out_level = raw->out_level,
  };
  gm2_moddelay_design(&rate_words, GM2_MODDELAY_DESIGN_TZFLANGER_POS, &design, config);
  return 0;
}
