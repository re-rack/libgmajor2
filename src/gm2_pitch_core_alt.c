#include "gm2_pitch_core_alt.h"

/* Both paths' words on the config's: each delay and the ratio pair step there
 * themselves, the feedbacks, the pan gains and the Mix words ride one-pole jobs.
 * The harmonizer's ratios follow a pitch that keeps moving, so they never settle
 * and a turn of the Intelligent Pitch Shifter's knobs is over when the rest are;
 * a turn that only moves those ratios starts no copy, which it cannot click. */
static int pitch_core_alt_settled(const gm2_pitch_core_state *state,
                                  const gm2_pitch_core_config *config) {
  int settled = state->wet == config->wet && state->dry == config->dry;
  for (unsigned i = 0; i < GM2_PITCH_CORE_VOICES; i++) {
    settled = settled && state->voice[i].delay == config->delay[i];
    settled = settled && state->feedback[i] == config->feedback[i];
    if (config->harmonize == 0) {
      settled = settled && state->ratio[i] == config->ratio[i];
    }
  }
  for (unsigned i = 0; i < 2u * GM2_PITCH_CORE_VOICES; i++) {
    settled = settled && state->pan[i] == config->pan[i];
  }
  return settled;
}

/* One stereo frame. A turn shows on the frame that ends a chunk, before the
 * routine's chunk work and its job run take it, so the copy starts from words that
 * all still belong to the old config. */
void gm2_pitch_core_alt_process_frame(gm2_pitch_core_alt_state *state,
                                      const gm2_pitch_core_config *config,
                                      const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  if (!state->handback.own && state->exact.chunk_pos == GM2_CHUNK_FRAMES - 1u &&
      !pitch_core_alt_settled(&state->exact, config)) {
    state->own = state->exact;
    gm2_handback_start(&state->handback);
  }

  int32_t exact[GM2_CHANNELS];
  gm2_pitch_core_process_frame(&state->exact, config, in, exact);
  if (!state->handback.own) {
    out[0] = exact[0];
    out[1] = exact[1];
    return;
  }

  int32_t own[GM2_CHANNELS];
  gm2_pitch_core_process_frame_depopped(&state->own, config, in, own);
  const int settled =
      pitch_core_alt_settled(&state->own, config) && pitch_core_alt_settled(&state->exact, config);
  (void)gm2_handback_frame(&state->handback, settled, exact, own, out);
}

void gm2_pitch_core_alt_state_init(gm2_pitch_core_alt_state *state,
                                   const gm2_pitch_core_config *config) {
  gm2_pitch_core_state_init(&state->exact, config);
  state->own = state->exact;
  gm2_handback_init(&state->handback);
}
