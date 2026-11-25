#include "gm2_achorus.h"

#include "gm2_process.h"

void gm2_achorus_state_init(gm2_achorus_state *state, const gm2_achorus_config *config) {
  gm2_moddelay_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(achorus, gm2_moddelay_core_process_frame)

void gm2_achorus_align(gm2_achorus_state *state, const gm2_achorus_config *config, double beats) {
  gm2_moddelay_core_align(state, config, beats);
}
