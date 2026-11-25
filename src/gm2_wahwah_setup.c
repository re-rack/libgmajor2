#include "gm2_wahwah_setup.h"

#include "gm2_dsp_math.h"
#include "gm2_mix.h"
#include "gm2_swf.h"
#include "gm2_wah_design.h"

/* The descriptor window P:0x0832CB clamps the raw Sweep word into; Mix's own
 * window is gm2_mix_words()'s. */
enum {
  SWEEP_MIN = 0,
  SWEEP_MAX = 100,
};

static const gm2_swf k_hundred = GM2_SWF_CONST(0x640000u, 0x86u); /* 100 */

int gm2_wahwah_setup_config(const gm2_context *context, const gm2_wahwah_setup_raw *raw,
                            gm2_wahwah_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }
  const int sweep = gm2_clamp_raw(raw->sweep, SWEEP_MIN, SWEEP_MAX);

  config->shift = gm2_wah_design_shift(raw->range);
  /* P:0x09250B: the divide leaves 100 % a hair above one, so the word rails. */
  config->sweep = gm2_swf_to_q23(gm2_swf_div(gm2_swf_from_int(sweep), k_hundred));
  gm2_mix_words(raw->out_level, raw->mix, &config->wet, &config->dry);
  return 0;
}
