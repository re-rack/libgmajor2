#include "gm2_wahwah.h"

#include "gm2_process.h"

/* P:0x09250B writes the Sweep parameter straight into Y:0x40E, so the config's
 * word is the one the routine reads. */
static void wahwah_frame(gm2_wahwah_state *state, const gm2_wahwah_config *config,
                         const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  state->sweep = config->sweep;
  (void)gm2_wah_core_process_frame(state, config->shift, config->wet, config->dry, in, out);
}

void gm2_wahwah_state_init(gm2_wahwah_state *state, const gm2_wahwah_config *config) {
  gm2_wah_core_state_init(state, config->sweep, config->wet, config->dry);
}

GM2_PROCESS_DEFINE(wahwah, wahwah_frame)
