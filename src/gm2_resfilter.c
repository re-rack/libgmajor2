#include "gm2_resfilter.h"

#include "gm2_process.h"

static void resfilter_frame(gm2_resfilter_state *state, const gm2_resfilter_config *config,
                            const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  (void)gm2_res_core_process_frame(state, config->coef, config->wet, config->dry, in, out);
}

void gm2_resfilter_state_init(gm2_resfilter_state *state, const gm2_resfilter_config *config) {
  gm2_res_core_state_init(state, config->coef, config->wet, config->dry);
}

GM2_PROCESS_DEFINE(resfilter, resfilter_frame)
