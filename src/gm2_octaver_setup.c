#include "gm2_octaver_setup.h"

#include "gm2_pitch_design.h"
#include "gm2_rate.h"

int gm2_octaver_setup_config(const gm2_context *context, const gm2_octaver_setup_raw *raw,
                             gm2_octaver_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  const gm2_pitch_design_raw design = {
      .direction = raw->direction,
      .range = raw->range,
  };
  gm2_pitch_design(&rate_words, GM2_PITCH_DESIGN_OCTAVER, &design, config);
  return 0;
}
