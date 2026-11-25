#include "gm2_resfilter_setup.h"

#include "gm2_dsp_math.h"
#include "gm2_mix.h"
#include "gm2_rate.h"
#include "gm2_res_design.h"
#include "gm2_swf.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  STYLE_MIN = 0,
  STYLE_MAX = 1,
  HI_CUT_MIN = 18,
  HI_CUT_MAX = 57,
};

int gm2_resfilter_setup_config(const gm2_context *context, const gm2_resfilter_setup_raw *raw,
                               gm2_resfilter_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }
  const int style = gm2_clamp_raw(raw->style, STYLE_MIN, STYLE_MAX);
  const int hi_cut = gm2_clamp_raw(raw->hi_cut, HI_CUT_MIN, HI_CUT_MAX);

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  gm2_res_design_coefs(&rate_words, style, gm2_swf_hz20(hi_cut),
                       gm2_res_design_resonance(style, raw->resonance), config->coef);
  gm2_mix_words(raw->out_level, raw->mix, &config->wet, &config->dry);
  return 0;
}
