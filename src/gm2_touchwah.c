#include "gm2_touchwah.h"

#include "gm2_dsp_math.h"
#include "gm2_process.h"
#include "gm2_tick.h"

static const gm2_swf k_sweep_min = GM2_SWF_CONST(0x800001u, 0x7Fu); /* just above -1 */
static const gm2_swf k_sweep_max = GM2_SWF_CONST(0x7FFFFFu, 0x7Fu); /* just below 1 */

/* The end of the tick P:0x092541: the sweep word the follower's level reads as,
 * through P:0x0924A7. */
static int32_t touchwah_sweep(const gm2_touchwah_config *config, gm2_swf follower) {
  gm2_swf value = gm2_swf_mac(config->centre, config->span, follower);
  if (gm2_swf_cmp(value, k_sweep_min) < 0) {
    value = k_sweep_min;
  }
  if (gm2_swf_cmp(value, k_sweep_max) > 0) {
    value = k_sweep_max;
  }
  return gm2_swf_to_q23(value);
}

/* 0xF29..0xF39: the envelope follows |L| + |R| of the halved gained inputs, at
 * once when that level is not below it and by one decay step otherwise. Returns
 * its accumulator, before the 24-bit store. */
static int64_t touchwah_envelope(gm2_touchwah_state *state, const gm2_touchwah_config *config,
                                 const int32_t in[GM2_CHANNELS]) {
  const int64_t left = gm2_mpyr(GM2_LEVEL_INPUT, gm2_s24((uint32_t)in[0]));
  const int64_t right = gm2_mpyr(gm2_s24((uint32_t)in[1]), GM2_LEVEL_INPUT);
  const int64_t level = gm2_acc56(gm2_abs56(gm2_asr_n(left, 1)) + gm2_abs56(gm2_asr_n(right, 1)));
  int64_t excess = gm2_sub24(level, state->envelope);
  if (excess < 0) {
    excess = gm2_mpy(config->decay, gm2_store24(excess));
  }
  const int64_t envelope = gm2_add24(excess, state->envelope);
  state->envelope = gm2_store24(envelope);
  return envelope;
}

/* One frame: the envelope, its peak hold on the chunk's first frame
 * (0xF3C..0xF4C), the routine, then the tick once its period of chunks is over. */
static void touchwah_frame(gm2_touchwah_state *state, const gm2_touchwah_config *config,
                           const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  const int64_t envelope = touchwah_envelope(state, config, in);
  if (state->core.chunk_pos == 0u) {
    gm2_tick_hold(&state->peak, envelope);
  }
  if (gm2_wah_core_process_frame(&state->core, config->shift, in, out) == 0) {
    return;
  }
  if (++state->tick < config->tick_chunks) {
    return;
  }
  state->tick = 0;
  /* P:0x092541: the peak hold as a level, the follower's step towards it, and the
   * sweep word Sense turns the follower into. */
  const int32_t level = gm2_tick_level(&state->peak);
  state->follower = gm2_tick_follow(state->follower, gm2_swf_from_q23(level),
                                    config->follow_threshold, config->follow_coeff);
  state->core.sweep = touchwah_sweep(config, state->follower);
}

void gm2_touchwah_state_init(gm2_touchwah_state *state, const gm2_touchwah_config *config) {
  state->envelope = 0;
  state->peak = GM2_W24_MIN;
  state->follower = gm2_swf_zero;
  state->tick = 0;
  gm2_wah_core_state_init(&state->core, touchwah_sweep(config, state->follower));
}

GM2_PROCESS_DEFINE(touchwah, touchwah_frame)
