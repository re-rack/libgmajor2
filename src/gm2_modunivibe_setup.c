#include "gm2_modunivibe_setup.h"

#include "gm2_rate.h"
#include "gm2_univibe_design.h"

int gm2_modunivibe_setup_config(const gm2_context *context, const gm2_modunivibe_setup_raw *raw,
                                gm2_modunivibe_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  const gm2_univibe_design_raw design = {
      .speed_1 = raw->speed_1,
      .speed_2 = raw->speed_2,
      .depth = raw->depth,
      .tempo_1 = raw->tempo_1,
      .tempo_2 = raw->tempo_2,
      .attack = raw->attack,
      .release = raw->release,
      .select = raw->select,
      .phase_reverse = raw->phase_reverse,
      .mix = raw->mix,
      .beat_ms = raw->beat_ms,
  };
  gm2_univibe_design(&rate_words, GM2_UNIVIBE_DESIGN_MODERN, &design, config);
  return 0;
}
