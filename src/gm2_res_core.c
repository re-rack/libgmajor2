#include "gm2_res_core.h"

#include "gm2_dsp_math.h"
#include "gm2_job.h"
#include "gm2_process.h"

/* Y:0x3CF and Y:0x3D0: the shift the routine applies to the gained input and
 * back to the clip's result. */
enum { RES_SHIFT = 3 };

/* One stereo frame, P:0x000EE9..0x000F23, then the chunk's job run once its last
 * frame is done. */
int gm2_res_core_process_frame(gm2_res_core_state *state, const int32_t coef[GM2_RES_CORE_COEFS],
                               int32_t wet, int32_t dry, const int32_t in[GM2_CHANNELS],
                               int32_t out[GM2_CHANNELS]) {
  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    /* 0x0EEF..0x0EF5: MPYR the input with Y:0x3EE, ASR by Y:0x3CF and store 24
     * bits: the shift leaves at most 2^20, so this store cannot rail. */
    int32_t *last = state->in[channel];
    int32_t x[3] = {last[0], last[1],
                    gm2_store24(gm2_asr_n(gm2_mpyr(GM2_LEVEL_INPUT, gm2_s24((uint32_t)in[channel])),
                                          RES_SHIFT))};
    last[1] = last[0];
    last[0] = x[2];

    /* 0x0EFB..0x0F0F: the two biquads in series. */
    int64_t acc = 0;
    for (unsigned biquad = 0; biquad < GM2_RES_CORE_BIQUADS; biquad++) {
      acc = gm2_biquad_step(
          &state->biquad[channel][biquad],
          &state->coef[GM2_BIQUAD_COEFS * (GM2_RES_CORE_BIQUADS * channel + biquad)], x);
    }

    /* 0x0F10..0x0F13: the soft clip P:0x0003AF with Y:0x3D0's shift, then the
     * 24-bit store of its result, at most the rail word shifted left three. */
    const int32_t clipped = gm2_store24(gm2_soft_clip(acc, gm2_soft_clip_words, RES_SHIFT));

    /* 0x0F15..0x0F21: the wet word and the clipped sample, the dry word and the
     * frame's input sample, one rounding over the sum. Each doubled product is at
     * most 2^47, so the 56-bit accumulator cannot wrap. */
    out[channel] =
        gm2_store24(gm2_round24(2 * (int64_t)state->wet * clipped +
                                2 * (int64_t)state->dry * gm2_s24((uint32_t)in[channel])));
  }

  if (++state->chunk_pos < GM2_CHUNK_FRAMES) {
    return 0;
  }
  state->chunk_pos = 0;
  /* P:0x00026F on the record X:0x09893C, then the level record */
  for (unsigned i = 0; i < GM2_RES_CORE_COEFS; i++) {
    state->coef[i] = gm2_job_smooth(state->coef[i], coef[i], GM2_JOB_SMOOTH_STEP);
  }
  state->wet = gm2_job_smooth(state->wet, wet, GM2_JOB_SMOOTH_STEP);
  state->dry = gm2_job_smooth(state->dry, dry, GM2_JOB_SMOOTH_STEP);
  return 1;
}

void gm2_res_core_state_init(gm2_res_core_state *state, const int32_t coef[GM2_RES_CORE_COEFS],
                             int32_t wet, int32_t dry) {
  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    for (unsigned i = 0; i < 2u; i++) {
      state->in[channel][i] = 0;
    }
    for (unsigned biquad = 0; biquad < GM2_RES_CORE_BIQUADS; biquad++) {
      gm2_biquad_init(&state->biquad[channel][biquad]);
    }
  }
  for (unsigned i = 0; i < GM2_RES_CORE_COEFS; i++) {
    state->coef[i] = coef[i];
  }
  state->wet = wet;
  state->dry = dry;
  state->chunk_pos = 0;
}
