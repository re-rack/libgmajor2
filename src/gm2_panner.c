#include "gm2_panner.h"

#include "gm2_process.h"

void gm2_panner_state_init(gm2_panner_state *state, const gm2_panner_config *config) {
  gm2_lfo_gain_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(panner, gm2_lfo_gain_core_process_frame)

void gm2_panner_align(gm2_panner_state *state, const gm2_panner_config *config, double beats) {
  gm2_lfo_gain_core_align(state, config, beats);
}
