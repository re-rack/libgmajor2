#include "gm2_dualdelay.h"

#include "gm2_process.h"

static void dualdelay_frame(gm2_dualdelay_state *state, const gm2_dualdelay_config *config,
                            const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  (void)gm2_delay_core_process_frame(state, config->coef, config->tap_offset, config->wet,
                                     config->dry, in, out);
}

void gm2_dualdelay_state_init(gm2_dualdelay_state *state, const gm2_dualdelay_config *config) {
  gm2_delay_core_state_init(state, config->coef, config->tap_offset, 0, config->wet, config->dry);
}

GM2_PROCESS_DEFINE(dualdelay, dualdelay_frame)
