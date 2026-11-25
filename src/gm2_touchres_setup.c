#include "gm2_touchres_setup.h"

#include "gm2_dsp_math.h"
#include "gm2_mix.h"
#include "gm2_rate.h"
#include "gm2_res_design.h"
#include "gm2_swf.h"
#include "gm2_tick.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  STYLE_MIN = 0,
  STYLE_MAX = 1,
};

int gm2_touchres_setup_config(const gm2_context *context, const gm2_touchres_setup_raw *raw,
                              gm2_touchres_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }
  const int style = gm2_clamp_raw(raw->style, STYLE_MIN, STYLE_MAX);

  gm2_rate_words_init(&config->rate, rate);
  gm2_res_design_tick_words_init(&config->words, style, raw->response, raw->sense, raw->freq_max);
  config->style = style;
  gm2_mix_words(raw->mix, &config->wet, &config->dry);
  config->tick_chunks = gm2_tick_chunks(rate);

  /* The words at rest are the ones the activation's own tick designs, at level 0. */
  gm2_swf follower = gm2_swf_zero;
  gm2_res_design_tick(&config->rate, &config->words, style, &follower, 0, config->coef);
  return 0;
}
