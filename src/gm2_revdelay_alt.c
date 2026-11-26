#include "gm2_revdelay_alt.h"

#include "gm2_process.h"

#include <string.h>

/* The delay's only gliding words are wet, dry and the coef words: a Delay or Tempo
 * turn leaves the grain's length and the taps it splices to the grain's own timer. */
static int revdelay_alt_settled(const gm2_revdelay_state *state,
                                const gm2_revdelay_alt_config *config) {
  return state->core.wet == config->wet && state->core.dry == config->dry &&
         memcmp(state->core.coef, config->coef, sizeof config->coef) == 0;
}

/* One stereo frame. A Delay or Tempo turn re-anchors the grain on the frame it
 * shows, so the copy starts there, and every later turn starts the hand-back's
 * wait over. */
static void revdelay_alt_frame(gm2_revdelay_alt_state *state, const gm2_revdelay_alt_config *config,
                               const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  if (state->exact.delay_ms != config->delay_ms) {
    if (!state->handback.own) {
      state->own = state->exact;
      /* The copy splices on its first frame, so it parts from the delay's path at
       * once instead of agreeing with it until the walk guard ends a grain. */
      state->own.delay_ms = config->delay_ms;
      state->own.tick = config->grain_chunks;
    }
    gm2_handback_start(&state->handback);
  }

  int32_t exact[GM2_CHANNELS];
  gm2_revdelay_process_frame(&state->exact, config, in, exact);
  if (!state->handback.own) {
    out[0] = exact[0];
    out[1] = exact[1];
    return;
  }

  int32_t own[GM2_CHANNELS];
  gm2_revdelay_advance(&state->own, config, 1, in, own);
  const int settled =
      revdelay_alt_settled(&state->own, config) && revdelay_alt_settled(&state->exact, config);
  (void)gm2_handback_frame(&state->handback, settled, exact, own, out);
}

void gm2_revdelay_alt_state_init(gm2_revdelay_alt_state *state,
                                 const gm2_revdelay_alt_config *config) {
  gm2_revdelay_state_init(&state->exact, config);
  state->own = state->exact;
  gm2_handback_init(&state->handback);
}

GM2_PROCESS_DEFINE(revdelay_alt, revdelay_alt_frame)
