#include "gm2_touchwah_setup.h"

#include "gm2_mix.h"
#include "gm2_rate.h"
#include "gm2_swf.h"
#include "gm2_tick.h"
#include "gm2_wah_design.h"

static const gm2_swf k_decay_rate = GM2_SWF_CONST(0x9C0000u, 0x85u); /* -50 a second */

int gm2_touchwah_setup_config(const gm2_context *context, const gm2_touchwah_setup_raw *raw,
                              gm2_touchwah_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  config->shift = gm2_wah_design_shift(raw->range);
  /* P:0x092678: one sample's decay, exp(-50 / rate). */
  config->decay = gm2_swf_to_q23(gm2_swf_exp(gm2_swf_mul(rate_words.period, k_decay_rate)));
  gm2_tick_response(raw->response, &config->follow_coeff, &config->follow_threshold);
  gm2_tick_sense(raw->sense, &config->centre, &config->span);
  gm2_mix_words(raw->out_level, raw->mix, &config->wet, &config->dry);
  config->tick_chunks = gm2_tick_chunks(rate);
  return 0;
}
