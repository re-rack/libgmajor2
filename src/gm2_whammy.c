#include "gm2_whammy.h"

#include "gm2_process.h"

void gm2_whammy_state_init(gm2_whammy_state *state, const gm2_whammy_config *config) {
  gm2_pitch_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(whammy, gm2_pitch_core_process_frame)
