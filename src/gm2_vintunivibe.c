#include "gm2_vintunivibe.h"

#include "gm2_process.h"

void gm2_vintunivibe_state_init(gm2_vintunivibe_state *state,
                                const gm2_vintunivibe_config *config) {
  gm2_univibe_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(vintunivibe, gm2_univibe_core_process_frame)

void gm2_vintunivibe_align(gm2_vintunivibe_state *state, const gm2_vintunivibe_config *config,
                           double beats) {
  gm2_univibe_core_align(state, config, beats);
}
