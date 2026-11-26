#include "gm2_mix.h"

#include "gm2_dsp_math.h"
#include "gm2_swf.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum { OUT_LEVEL_MIN = -100, OUT_LEVEL_MAX = 0, MIX_MIN = 0, MIX_MAX = 100 };

static const gm2_swf k_pi_2 = GM2_SWF_CONST(0x6487EEu, 0x80u); /* 1.5707965 */

int32_t gm2_mix_curve(int32_t word) {
  return gm2_swf_to_q23(gm2_swf_sin(gm2_swf_mul(gm2_swf_from_q23(word), k_pi_2)));
}

void gm2_mix_words(int out_level, int mix, int32_t *wet, int32_t *dry) {
  const int32_t gain = gm2_swf_db_to_lin(gm2_clamp_raw(out_level, OUT_LEVEL_MIN, OUT_LEVEL_MAX));
  const int percent = gm2_clamp_raw(mix, MIX_MIN, MIX_MAX);
  const int32_t staged = gm2_swf_to_q23(gm2_swf_mul(gm2_swf_from_int(percent), gm2_swf_hundredth));
  *wet = gm2_store24(gm2_mpyr(gain, gm2_mix_curve(staged)));
  *dry = gm2_store24(gm2_mpyr(gain, gm2_mix_curve(GM2_W24_MAX - staged)));
}
