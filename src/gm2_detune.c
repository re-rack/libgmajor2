#include "gm2_detune.h"

#include "gm2_process.h"

void gm2_detune_state_init(gm2_detune_state *state, const gm2_detune_config *config) {
  gm2_pitch_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(detune, gm2_pitch_core_process_frame)
