#include "gm2_cchorus.h"

#include "gm2_process.h"

void gm2_cchorus_state_init(gm2_cchorus_state *state, const gm2_cchorus_config *config) {
  gm2_moddelay_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(cchorus, gm2_moddelay_core_process_frame)

void gm2_cchorus_align(gm2_cchorus_state *state, const gm2_cchorus_config *config, double beats) {
  gm2_moddelay_core_align(state, config, beats);
}
