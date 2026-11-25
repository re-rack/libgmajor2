/* The six pairs P:0x082DC0 writes to X:0x099429..0x099434 after P:0x000BFD
 * converts the rate, transcribed on the firmware's software float.
 */
#include "gm2_rate.h"

#include "gm2_swf.h"

#include <stdint.h>

static const gm2_swf k_milli = GM2_SWF_CONST(0x418937u, 0x76u); /* 0.001 */
static const gm2_swf k_two_pi = GM2_SWF_CONST(0x6487EEu, 0x82u); /* 2 pi */
static const gm2_swf k_thousand = GM2_SWF_CONST(0x7D0000u, 0x89u); /* 1000 */

void gm2_rate_words_init(gm2_rate_words *words, unsigned rate) {
  const gm2_swf hz = gm2_swf_from_int((int32_t)rate);

  words->rate = hz;
  words->rate_khz = gm2_swf_mul(k_milli, hz);
  words->omega = gm2_swf_div(k_two_pi, hz);
  words->minus_omega = gm2_swf_neg(words->omega);
  words->period = gm2_swf_div(gm2_swf_one, hz);
  words->period_ms = gm2_swf_div(k_thousand, hz);
}
