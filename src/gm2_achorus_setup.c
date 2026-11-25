#include "gm2_achorus_setup.h"

#include "gm2_moddelay_design.h"
#include "gm2_rate.h"

int gm2_achorus_setup_config(const gm2_context *context, const gm2_achorus_setup_raw *raw,
                             gm2_achorus_config *config) {
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
      .hi_cut = raw->hi_cut,
      .delay = raw->delay,
      .golden_ratio = raw->golden_ratio,
      .phase_reverse = raw->phase_reverse,
      .beat_ms = raw->beat_ms,
  };
  gm2_moddelay_design(&rate_words, GM2_MODDELAY_DESIGN_ACHORUS, &design, config);
  return 0;
}
