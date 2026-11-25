#include "gm2_tremolo.h"

#include "gm2_process.h"

void gm2_tremolo_state_init(gm2_tremolo_state *state, const gm2_tremolo_config *config) {
  gm2_lfo_gain_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(tremolo, gm2_lfo_gain_core_process_frame)

void gm2_tremolo_align(gm2_tremolo_state *state, const gm2_tremolo_config *config, double beats) {
  gm2_lfo_gain_core_align(state, config, beats);
}
