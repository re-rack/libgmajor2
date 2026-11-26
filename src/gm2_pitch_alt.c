#include "gm2_pitch_alt.h"

#include "gm2_process.h"

void gm2_pitch_alt_state_init(gm2_pitch_alt_state *state, const gm2_pitch_alt_config *config) {
  gm2_pitch_core_alt_state_init(state, config);
}

GM2_PROCESS_DEFINE(pitch_alt, gm2_pitch_core_alt_process_frame)
