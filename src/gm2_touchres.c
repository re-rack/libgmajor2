#include "gm2_touchres.h"

#include "gm2_process.h"
#include "gm2_tick.h"

/* One frame: the detector, the routine, then the tick once its period of chunks
 * is over. The detector takes its level on the chunk's first frame, as the gate's
 * own gain computer does. */
static void touchres_frame(gm2_touchres_state *state, const gm2_touchres_config *config,
                           const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  gm2_gate_detector_frame(&state->detector, in, state->core.chunk_pos);
  if (gm2_res_core_process_frame(&state->core, state->coef, in, out) == 0) {
    return;
  }
  if (++state->tick < config->tick_chunks) {
    return;
  }
  state->tick = 0;
  /* P:0x090494 reads and resets the gate's peak hold, P:0x091CF9 designs from it. */
  const int32_t level = gm2_tick_level(&state->detector.peak);
  gm2_res_design_tick(&config->rate, &config->words, config->style, &state->follower, level,
                      state->coef);
}

void gm2_touchres_state_init(gm2_touchres_state *state, const gm2_touchres_config *config) {
  gm2_res_core_state_init(&state->core, config->coef);
  for (unsigned i = 0; i < GM2_RES_CORE_COEFS; i++) {
    state->coef[i] = config->coef[i];
  }
  state->follower = gm2_swf_zero;
  gm2_gate_detector_init(&state->detector);
  state->tick = 0;
}

GM2_PROCESS_DEFINE(touchres, touchres_frame)
