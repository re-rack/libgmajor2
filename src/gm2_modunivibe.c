#include "gm2_modunivibe.h"

#include "gm2_process.h"

void gm2_modunivibe_state_init(gm2_modunivibe_state *state, const gm2_modunivibe_config *config) {
  gm2_univibe_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(modunivibe, gm2_univibe_core_process_frame)

void gm2_modunivibe_align(gm2_modunivibe_state *state, const gm2_modunivibe_config *config,
                          double beats) {
  gm2_univibe_core_align(state, config, beats);
}
