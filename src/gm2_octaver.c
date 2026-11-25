#include "gm2_octaver.h"

#include "gm2_process.h"

void gm2_octaver_state_init(gm2_octaver_state *state, const gm2_octaver_config *config) {
  gm2_pitch_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(octaver, gm2_pitch_core_process_frame)
