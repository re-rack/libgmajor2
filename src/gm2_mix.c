#include "gm2_mix.h"

#include "gm2_dsp_math.h"
#include "gm2_swf.h"

/* The descriptor window P:0x0832CB clamps the Mix value into. */
enum { MIX_MIN = 0, MIX_MAX = 100 };

static const gm2_swf k_cent = GM2_SWF_CONST(0x51EB85u, 0x79u); /* 0.01 */
static const gm2_swf k_pi_2 = GM2_SWF_CONST(0x6487EEu, 0x80u); /* 1.5707965 */

int32_t gm2_mix_curve(int32_t word) {
  return gm2_swf_to_q23(gm2_swf_sin(gm2_swf_mul(gm2_swf_from_q23(word), k_pi_2)));
}

void gm2_mix_words(int raw, int32_t *wet, int32_t *dry) {
  const int mix = gm2_clamp_raw(raw, MIX_MIN, MIX_MAX);
  const int32_t staged = gm2_swf_to_q23(gm2_swf_mul(gm2_swf_from_int(mix), k_cent));
  /* Out Level 0 dB, the word P:0x082DF1 stages for it, scales both halves. */
  *wet = gm2_store24(gm2_mpyr(GM2_W24_MAX, gm2_mix_curve(staged)));
  *dry = gm2_store24(gm2_mpyr(GM2_W24_MAX, gm2_mix_curve(GM2_W24_MAX - staged)));
}
