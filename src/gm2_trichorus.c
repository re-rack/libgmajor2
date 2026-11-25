#include "gm2_trichorus.h"

#include "gm2_process.h"

void gm2_trichorus_state_init(gm2_trichorus_state *state, const gm2_trichorus_config *config) {
  gm2_moddelay_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(trichorus, gm2_moddelay_core_process_frame)

void gm2_trichorus_align(gm2_trichorus_state *state, const gm2_trichorus_config *config,
                         double beats) {
  gm2_moddelay_core_align(state, config, beats);
}
