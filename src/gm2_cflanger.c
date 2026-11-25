#include "gm2_cflanger.h"

#include "gm2_process.h"

void gm2_cflanger_state_init(gm2_cflanger_state *state, const gm2_cflanger_config *config) {
  gm2_moddelay_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(cflanger, gm2_moddelay_core_process_frame)

void gm2_cflanger_align(gm2_cflanger_state *state, const gm2_cflanger_config *config,
                        double beats) {
  gm2_moddelay_core_align(state, config, beats);
}
