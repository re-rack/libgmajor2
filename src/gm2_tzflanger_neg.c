#include "gm2_tzflanger_neg.h"

#include "gm2_process.h"

void gm2_tzflanger_neg_state_init(gm2_tzflanger_neg_state *state,
                                  const gm2_tzflanger_neg_config *config) {
  gm2_moddelay_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(tzflanger_neg, gm2_moddelay_core_process_frame)

void gm2_tzflanger_neg_align(gm2_tzflanger_neg_state *state, const gm2_tzflanger_neg_config *config,
                             double beats) {
  gm2_moddelay_core_align(state, config, beats);
}
