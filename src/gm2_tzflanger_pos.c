#include "gm2_tzflanger_pos.h"

#include "gm2_process.h"

void gm2_tzflanger_pos_state_init(gm2_tzflanger_pos_state *state,
                                  const gm2_tzflanger_pos_config *config) {
  gm2_moddelay_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(tzflanger_pos, gm2_moddelay_core_process_frame)

void gm2_tzflanger_pos_align(gm2_tzflanger_pos_state *state, const gm2_tzflanger_pos_config *config,
                             double beats) {
  gm2_moddelay_core_align(state, config, beats);
}
