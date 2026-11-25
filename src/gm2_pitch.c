#include "gm2_pitch.h"

#include "gm2_process.h"

void gm2_pitch_state_init(gm2_pitch_state *state, const gm2_pitch_config *config) {
  gm2_pitch_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(pitch, gm2_pitch_core_process_frame)
