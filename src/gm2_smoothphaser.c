#include "gm2_smoothphaser.h"

#include "gm2_process.h"

void gm2_smoothphaser_state_init(gm2_smoothphaser_state *state,
                                 const gm2_smoothphaser_config *config) {
  gm2_phaser_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(smoothphaser, gm2_phaser_core_process_frame)

void gm2_smoothphaser_align(gm2_smoothphaser_state *state, const gm2_smoothphaser_config *config,
                            double beats) {
  gm2_phaser_core_align(state, config, beats);
}
