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
  const int32_t *c = state->coef;
  const int32_t *o = config->offsets;
  const uint32_t origin = state->write;

  /* 0x12D7..0x12E5: the mono input at the write point, where both pointers
   * start. */
  const int32_t in_l = gm2_s24((uint32_t)in[0]);
  const int32_t in_r = gm2_s24((uint32_t)in[1]);
  int64_t b = 2 * (int64_t)GM2_LEVEL_INPUT * in_l;
  b += 2 * (int64_t)in_r * GM2_LEVEL_INPUT;
  state->buffer[origin] = gm2_store24(gm2_asr_n(b, 2));

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

  /* 0x13C6..0x13DE: the Room and Reverb levels, the pre-delay tap's gain, then
   * the Mix pair: the wet word ASL'd on each channel's sum and the dry word on
   * that channel's input sample, the two rounded together once. */
  int64_t right = 2 * (int64_t)c[GM2_REVERB_ROOM] * state->early_out[1];
  int64_t left = 2 * (int64_t)c[GM2_REVERB_ROOM + 1] * state->early_out[0];
  right += 2 * (int64_t)c[GM2_REVERB_LEVEL] * state->tank_out[1];
  left += 2 * (int64_t)c[GM2_REVERB_LEVEL + 1] * state->tank_out[0];
  state->pre_delay = gm2_store24(2 * (int64_t)c[GM2_REVERB_PRE_DELAY] * pre_delay);
  const int32_t wet = state->wet;
  const int32_t dry = state->dry;
  const int64_t wet_l = gm2_asl(2 * (int64_t)wet * gm2_store24(left));
  const int64_t wet_r = gm2_asl(2 * (int64_t)wet * gm2_store24(right));
  out[0] = gm2_store24(gm2_round24(wet_l + 2 * (int64_t)in_l * dry));
  out[1] = gm2_store24(gm2_round24(wet_r + 2 * (int64_t)in_r * dry));

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
