#include "gm2_ipitch.h"

#include "gm2_process.h"

void gm2_ipitch_state_init(gm2_ipitch_state *state, const gm2_ipitch_config *config) {
  gm2_pitch_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(ipitch, gm2_pitch_core_process_frame)
