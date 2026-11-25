#include "gm2_ipitch_setup.h"

#include "gm2_pitch_design.h"
#include "gm2_rate.h"

int gm2_ipitch_setup_config(const gm2_context *context, const gm2_ipitch_setup_raw *raw,
                            gm2_ipitch_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  const gm2_pitch_design_raw design = {
      .voice = {raw->voice_1, raw->voice_2},
      .pan = {raw->pan_1, raw->pan_2},
      .delay = {raw->delay_1, raw->delay_2},
      .level = {raw->level_1, raw->level_2},
      .key = raw->key,
      .scale = raw->scale,
      .mix = raw->mix,
      .out_level = raw->out_level,
  };
  gm2_pitch_design(&rate_words, GM2_PITCH_DESIGN_IPITCH, &design, config);
  return 0;
}
