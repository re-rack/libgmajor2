#include "gm2_vibrato.h"

#include "gm2_process.h"

void gm2_vibrato_state_init(gm2_vibrato_state *state, const gm2_vibrato_config *config) {
  gm2_moddelay_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(vibrato, gm2_moddelay_core_process_frame)

void gm2_vibrato_align(gm2_vibrato_state *state, const gm2_vibrato_config *config, double beats) {
  gm2_moddelay_core_align(state, config, beats);
}
