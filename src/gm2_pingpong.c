#include "gm2_pingpong.h"

#include "gm2_process.h"

static void pingpong_frame(gm2_pingpong_state *state, const gm2_pingpong_config *config,
                           const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  (void)gm2_delay_core_process_frame(state, config->coef, config->tap_offset, config->wet,
                                     config->dry, in, out);
}

void gm2_pingpong_state_init(gm2_pingpong_state *state, const gm2_pingpong_config *config) {
  gm2_delay_core_state_init(state, config->coef, config->tap_offset, 0, config->wet, config->dry);
}

GM2_PROCESS_DEFINE(pingpong, pingpong_frame)
