#include "gm2_resfilter_alt.h"

#include "gm2_process.h"

#include <string.h>

/* One stereo frame. A turn shows on the frame that ends a chunk, before the
 * filter's job run takes it, so the copy starts from words that all still belong
 * to the old design. */
static void resfilter_alt_frame(gm2_resfilter_alt_state *state,
                                const gm2_resfilter_alt_config *config,
                                const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  if (!state->handback.own && state->exact.chunk_pos == GM2_CHUNK_FRAMES - 1u &&
      memcmp(state->exact.coef, config->coef, sizeof config->coef) != 0) {
    state->own = state->exact;
    memcpy(state->to, state->exact.coef, sizeof state->to);
    gm2_handback_start(&state->handback);
  }

  int32_t exact[GM2_CHANNELS];
  gm2_resfilter_process_frame(&state->exact, config, in, exact);
  if (!state->handback.own) {
    out[0] = exact[0];
    out[1] = exact[1];
    return;
  }

  /* The routine on the copy's own words, which its job run then leaves be. */
  int32_t live[GM2_RES_CORE_COEFS];
  memcpy(live, state->own.coef, sizeof live);
  int32_t own[GM2_CHANNELS];
  if (gm2_res_core_process_frame(&state->own, live, config->wet, config->dry, in, own) != 0) {
    (void)gm2_job_group_run(&state->group, state->own.coef, state->from, state->to, config->coef,
                            GM2_RES_CORE_COEFS);
  }
  const int settled = memcmp(state->own.coef, config->coef, sizeof config->coef) == 0 &&
                      memcmp(state->exact.coef, config->coef, sizeof config->coef) == 0;
  (void)gm2_handback_frame(&state->handback, settled, exact, own, out);
}

void gm2_resfilter_alt_state_init(gm2_resfilter_alt_state *state,
                                  const gm2_resfilter_alt_config *config) {
  gm2_resfilter_state_init(&state->exact, config);
  state->own = state->exact;
  memcpy(state->from, config->coef, sizeof state->from);
  memcpy(state->to, config->coef, sizeof state->to);
  state->group.remain = 0;
  gm2_handback_init(&state->handback);
}

GM2_PROCESS_DEFINE(resfilter_alt, resfilter_alt_frame)
