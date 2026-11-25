#include "gm2_aflanger.h"

#include "gm2_process.h"

void gm2_aflanger_state_init(gm2_aflanger_state *state, const gm2_aflanger_config *config) {
  gm2_moddelay_core_state_init(state, config);
}

GM2_PROCESS_DEFINE(aflanger, gm2_moddelay_core_process_frame)

void gm2_aflanger_align(gm2_aflanger_state *state, const gm2_aflanger_config *config,
                        double beats) {
  gm2_moddelay_core_align(state, config, beats);
}
