#include "gm2_eq.h"

#include "gm2_dsp_math.h"
#include "gm2_job.h"
#include "gm2_process.h"

#include <string.h>

/* The chunk's job run: the fifteen words share the one-pole job P:0x00026F. A
 * word already on its staged value is settled, since the job then takes the
 * staged word itself, so a chunk with nothing to move returns here. */
static void eq_jobs(gm2_eq_state *state, const gm2_eq_config *config) {
  if (memcmp(state->coef, config->coef, sizeof state->coef) == 0) {
    return;
  }
  for (unsigned i = 0; i < GM2_EQ_COEFS; i++) {
    state->coef[i] = gm2_job_smooth(state->coef[i], config->coef[i], GM2_JOB_SMOOTH_STEP);
  }
}

/* One stereo frame, P:0x000CC6..0x000D13, then the chunk's job run once its last
 * frame is done. */
static void eq_frame(gm2_eq_state *state, const gm2_eq_config *config,
                     const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    /* 0xCC6..0xCCB: the input gain. */
    int32_t *last = state->in[channel];
    int32_t x[3] = {last[0], last[1],
                    gm2_store24(gm2_mpyr(GM2_LEVEL_INPUT, gm2_s24((uint32_t)in[channel])))};
    last[1] = last[0];
    last[0] = x[2];

    /* 0xCCD..0xD07: the three bands in series. */
    const int32_t *coef = state->coef;
    gm2_biquad *const band = state->band[channel];
    (void)gm2_biquad_step(&band[0], coef, x);
    (void)gm2_biquad_step(&band[1], coef + GM2_BIQUAD_COEFS, x);
    (void)gm2_biquad_step(&band[2], coef + 2u * GM2_BIQUAD_COEFS, x);

    /* 0xD08..0xD13: the wet word; the zero dry term's MACR still rounds. */
    out[channel] = gm2_store24(gm2_round24(gm2_mpy(GM2_LEVEL_WET, x[2])));
  }

  if (++state->chunk_pos < GM2_CHUNK_FRAMES) {
    return;
  }
  state->chunk_pos = 0;
  eq_jobs(state, config);
}

void gm2_eq_state_init(gm2_eq_state *state, const gm2_eq_config *config) {
  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    state->in[channel][0] = 0;
    state->in[channel][1] = 0;
    for (unsigned band = 0; band < GM2_EQ_BANDS; band++) {
      gm2_biquad_init(&state->band[channel][band]);
    }
  }
  for (unsigned i = 0; i < GM2_EQ_COEFS; i++) {
    state->coef[i] = config->coef[i];
  }
  state->chunk_pos = 0;
}

GM2_PROCESS_DEFINE(eq, eq_frame)
