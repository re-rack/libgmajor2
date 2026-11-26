#include "gm2_tick.h"

#include "gm2_base.h"
#include "gm2_dsp_math.h"
#include "gm2_tables.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  RESPONSE_MIN = 0,
  RESPONSE_MAX = 2,
  SENSE_MIN = 0,
  SENSE_MAX = 10,
};

static const gm2_swf k_level_scale = GM2_SWF_CONST(0x7B4D49u, 0x82u); /* 7.706 */
static const gm2_swf k_minus_23 = GM2_SWF_CONST(0xA40000u, 0x84u);     /* -23 */
static const gm2_swf k_sense_step = GM2_SWF_CONST(0x5D1746u, 0x7Cu);   /* 1/11 */
static const gm2_swf k_sense_span = GM2_SWF_CONST(0x4CCCCDu, 0x7Fu);   /* 0.6 */

/* The follower's time for Slow, Medium and Fast: 3/10000, 2/10000 and 1/10000. */
static const gm2_swf k_response[RESPONSE_MAX + 1] = {
    GM2_SWF_CONST(0x4EA4A9u, 0x74u), /* 0.0003 */
    GM2_SWF_CONST(0x68DB8Cu, 0x73u), /* 0.0002 */
    GM2_SWF_CONST(0x68DB8Cu, 0x72u), /* 0.0001 */
};

unsigned gm2_tick_period(unsigned rate, unsigned ms) {
  const uint64_t frames = (uint64_t)ms * rate;
  const uint64_t chunks = (frames + 500u * GM2_CHUNK_FRAMES) / (1000u * GM2_CHUNK_FRAMES);
  return chunks == 0u ? 1u : (unsigned)chunks;
}

unsigned gm2_tick_chunks(unsigned rate) { return gm2_tick_period(rate, GM2_TICK_MS); }

void gm2_tick_hold(int32_t *peak, int64_t envelope) {
  const int64_t level = gm2_log2(envelope);
  if (gm2_word_acc(*peak) < level) {
    *peak = gm2_store24(level);
  }
}

int32_t gm2_tick_level(int32_t *peak) {
  const gm2_swf level = gm2_swf_mac(gm2_swf_one, k_level_scale, gm2_swf_from_q23(*peak));
  *peak = GM2_W24_MIN;
  const int32_t word = gm2_swf_to_q23(level);
  return word < 0 ? 0 : word;
}

gm2_swf gm2_tick_follow(gm2_swf state, gm2_swf level, gm2_swf threshold, gm2_swf coeff) {
  gm2_swf gap = gm2_swf_sub(level, state);
  if (gm2_swf_cmp(gap, threshold) > 0) {
    gap = gm2_swf_mul(gap, coeff);
  }
  return gm2_swf_add(gap, state);
}

void gm2_tick_response(int response, gm2_swf *coeff, gm2_swf *threshold) {
  const int speed = gm2_clamp_raw(response, RESPONSE_MIN, RESPONSE_MAX);

  const gm2_swf tick_rate = gm2_swf_div(gm2_swf_thousandth, gm2_swf_from_int(GM2_TICK_MS));

  *coeff = gm2_swf_sub(
      gm2_swf_one,
      gm2_swf_exp(gm2_swf_div(gm2_swf_minus_one, gm2_swf_div(k_response[speed], tick_rate))));
  *threshold = gm2_swf_mul(gm2_swf_pow(gm2_swf_two, k_minus_23),
                           gm2_swf_add(gm2_swf_div(gm2_swf_one, *coeff), gm2_swf_one));
}

void gm2_tick_sense(int sense, gm2_swf *centre, gm2_swf *span) {
  const int level = gm2_clamp_raw(sense, SENSE_MIN, SENSE_MAX);

  const gm2_swf zero = gm2_swf_mul(gm2_swf_from_int(SENSE_MAX - level), k_sense_step);
  gm2_swf full = gm2_swf_add(k_sense_span, zero);
  if (gm2_swf_cmp(full, gm2_swf_one) > 0) {
    full = gm2_swf_one;
  }
  *span = gm2_swf_div(gm2_swf_one, gm2_swf_sub(full, zero));
  *centre = gm2_swf_msub(gm2_swf_zero, zero, *span);
}
