#include "gm2_reverb.h"

#include "gm2_dsp_math.h"
#include "gm2_job.h"
#include "gm2_process.h"
#include "gm2_reverb_kernel.h"

#include <string.h>

/* The chunk's job run: the eighty words and the Mix pair ride one-pole jobs. A
 * word already on its staged value is settled, since the job then takes the
 * staged word itself, so a chunk with nothing to move returns here. */
static void reverb_jobs(gm2_reverb_state *state, const gm2_reverb_config *config) {
  if (state->wet == config->wet && state->dry == config->dry &&
      memcmp(state->coef, config->coef, sizeof state->coef) == 0) {
    return;
  }
  for (unsigned i = 0; i < GM2_REVERB_COEFS; i++) {
    state->coef[i] = gm2_job_smooth(state->coef[i], config->coef[i], GM2_JOB_SMOOTH_STEP);
  }
  state->wet = gm2_job_smooth(state->wet, config->wet, GM2_JOB_SMOOTH_STEP);
  state->dry = gm2_job_smooth(state->dry, config->dry, GM2_JOB_SMOOTH_STEP);
}

/* One stereo frame, P:0x0012D2..0x0013DE, then the chunk's job run once its last
 * frame is done. */
static void reverb_frame(gm2_reverb_state *state, const gm2_reverb_config *config,
                         const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  const int32_t *o = config->offsets;
  const uint32_t origin = state->write;

  const int32_t in_l = gm2_s24((uint32_t)in[0]);
  const int32_t in_r = gm2_s24((uint32_t)in[1]);
  gm2_reverb_kernel_input(state, in_l, in_r, origin);

  gm2_reverb_kernel_tank(state, o, config->tank_signs, origin);

  gm2_reverb_kernel_walk walk = gm2_reverb_kernel_walk_start(o, origin);
  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    state->early_out[channel] =
        gm2_reverb_kernel_early(state, &walk, config->early[channel], state->comb[channel],
                                state->biquad[channel], channel);
  }

  /* 0x13B8..0x13C5: the pre-delay tap, and the write point a word on. */
  const int32_t pre_delay =
      state->buffer[gm2_reverb_kernel_step(origin, o[GM2_REVERB_KERNEL_PRE_DELAY])];
  state->write = (origin + 1u) & (GM2_REVERB_BUFFER_WORDS - 1u);

  gm2_reverb_kernel_output(state, pre_delay, in_l, in_r, out);

  if (++state->chunk_pos < GM2_CHUNK_FRAMES) {
    return;
  }
  state->chunk_pos = 0;
  reverb_jobs(state, config);
}

void gm2_reverb_state_init(gm2_reverb_state *state, const gm2_reverb_config *config) {
  memset(state, 0, sizeof *state);
  for (unsigned i = 0; i < GM2_REVERB_COEFS; i++) {
    state->coef[i] = config->coef[i];
  }
  state->wet = config->wet;
  state->dry = config->dry;
}

GM2_PROCESS_DEFINE(reverb, reverb_frame)
