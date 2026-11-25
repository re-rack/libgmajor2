#include "gm2_dyndelay.h"

#include "gm2_delay_design.h"
#include "gm2_dsp_math.h"
#include "gm2_process.h"
#include "gm2_tick.h"

#include <string.h>

/* P:0x0944BE: one tick of the ducking envelope. Above the Sense level it walks
 * towards the Damp word by the attack share of the gap, otherwise back towards
 * full scale by the release share. */
static int32_t dyndelay_duck(int32_t duck, int32_t level, const int32_t *words) {
  int32_t b = duck;
  if (level > words[GM2_DYNDELAY_DUCK_SENSE]) {
    const int32_t diff = gm2_store24(gm2_sub24(gm2_word_acc(words[GM2_DYNDELAY_DUCK_DAMP]), b));
    b = gm2_store24(gm2_macr(gm2_word_acc(b), diff, words[GM2_DYNDELAY_DUCK_ATTACK]));
  } else {
    const int32_t diff = gm2_store24(gm2_sub24(gm2_word_acc(GM2_W24_MAX), b));
    b = gm2_store24(gm2_macr(gm2_word_acc(b), diff, words[GM2_DYNDELAY_DUCK_RELEASE]));
  }
  return b;
}

/* The gain P:0x0944BE stages in the output matrix for an envelope word. */
static int32_t dyndelay_gain(int32_t duck) {
  return gm2_store24(gm2_mpyr(duck, GM2_DELAY_DESIGN_WET_SCALE));
}

/* The routine's words with the ducking gain as the output matrix's first pair. */
static void dyndelay_coef(const gm2_dyndelay_state *state, const gm2_dyndelay_config *config,
                          int32_t coef[GM2_DELAY_CORE_COEFS]) {
  memcpy(coef, config->coef, sizeof config->coef);
  coef[GM2_DELAY_CORE_OUTPUT] = state->duck_gain;
  coef[GM2_DELAY_CORE_OUTPUT + 1u] = state->duck_gain;
}

/* One frame: the detector, the routine with the ducking gain as the output
 * matrix's wet pair, then the tick once its period of chunks is over. The
 * detector takes its level on the chunk's first frame, as the gate's own gain
 * computer does. */
static void dyndelay_frame(gm2_dyndelay_state *state, const gm2_dyndelay_config *config,
                           const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  int32_t coef[GM2_DELAY_CORE_COEFS];
  dyndelay_coef(state, config, coef);

  gm2_gate_detector_frame(&state->detector, in, state->core.chunk_pos);
  if (gm2_delay_core_process_frame(&state->core, coef, config->tap_offset, in, out) == 0) {
    return;
  }
  if (++state->tick < config->tick_chunks) {
    return;
  }
  state->tick = 0;
  /* P:0x090494 reads and resets the gate's peak hold. */
  const int32_t level = gm2_tick_level(&state->detector.peak);
  state->duck = dyndelay_duck(state->duck, level, config->duck);
  state->duck_gain = dyndelay_gain(state->duck);
}

void gm2_dyndelay_state_init(gm2_dyndelay_state *state, const gm2_dyndelay_config *config) {
  /* At rest the envelope sits on the word a tick in silence leaves where it is. */
  int32_t duck = 0;
  for (;;) {
    const int32_t next = dyndelay_duck(duck, 0, config->duck);
    if (next == duck) {
      break;
    }
    duck = next;
  }
  state->duck = duck;
  state->duck_gain = dyndelay_gain(duck);

  int32_t coef[GM2_DELAY_CORE_COEFS];
  dyndelay_coef(state, config, coef);
  gm2_delay_core_state_init(&state->core, coef, config->tap_offset, 0);
  gm2_gate_detector_init(&state->detector);
  state->tick = 0;
}

GM2_PROCESS_DEFINE(dyndelay, dyndelay_frame)
