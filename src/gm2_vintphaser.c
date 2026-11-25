#include "gm2_vintphaser.h"

#include "gm2_process.h"

void gm2_vintphaser_state_init(gm2_vintphaser_state *state, const gm2_vintphaser_config *config) {
  gm2_phaser_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(vintphaser, gm2_phaser_core_process_frame)

void gm2_vintphaser_align(gm2_vintphaser_state *state, const gm2_vintphaser_config *config,
                          double beats) {
  gm2_phaser_core_align(state, config, beats);
}
