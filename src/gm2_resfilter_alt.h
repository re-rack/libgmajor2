/* The de-popped Resonance Filter: gm2_resfilter.h with a group glide (gm2_job.h)
 * on a Style, Hi-Cut or Resonance turn, handed back to the filter's own path once
 * settled (gm2_handback.h).
 *
 * At low cutoffs the filter's feed-forward words are a few hundred at most, so the
 * one-pole job lands them on a turn's new words at once while the feedback words
 * still glide, and a fast upward sweep of Hi-Cut bursts by up to 17 dB. Here a
 * turn starts a copy of the filter whose twenty words glide together, and plays
 * it until the filter itself has settled again.
 */
#ifndef GM2_RESFILTER_ALT_H
#define GM2_RESFILTER_ALT_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_base.h"
#include "gm2_handback.h"
#include "gm2_job.h"
#include "gm2_res_core.h"
#include "gm2_resfilter.h"

typedef gm2_resfilter_config gm2_resfilter_alt_config;

typedef struct {
  gm2_resfilter_state exact; /* the filter, always run */
  gm2_resfilter_state own;   /* the de-popped copy, run from a turn until the hand-back */
  int32_t from[GM2_RES_CORE_COEFS];
  int32_t to[GM2_RES_CORE_COEFS];
  gm2_job_group group;
  gm2_handback handback;
} gm2_resfilter_alt_state;

void gm2_resfilter_alt_state_init(gm2_resfilter_alt_state *state,
                                  const gm2_resfilter_alt_config *config);

void gm2_resfilter_alt_process_split(gm2_resfilter_alt_state *state,
                                     const gm2_resfilter_alt_config *config, const int32_t *in_l,
                                     const int32_t *in_r, int32_t *out_l, int32_t *out_r,
                                     size_t frames);

void gm2_resfilter_alt_process_f32(gm2_resfilter_alt_state *state,
                                   const gm2_resfilter_alt_config *config, const float *in_l,
                                   const float *in_r, float *out_l, float *out_r, size_t frames);

void gm2_resfilter_alt_process_interleaved(gm2_resfilter_alt_state *state,
                                           const gm2_resfilter_alt_config *config,
                                           const int32_t *in, int32_t *out, size_t frames);

void gm2_resfilter_alt_process_frame(gm2_resfilter_alt_state *state,
                                     const gm2_resfilter_alt_config *config,
                                     const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_RESFILTER_ALT_H */
