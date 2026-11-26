/* The de-popped three-band EQ: gm2_eq.h with a group glide (gm2_job.h) on a knob
 * turn, handed back to the EQ's own path once settled (gm2_handback.h).
 *
 * The EQ's one-pole job lands each biquad word on its own, so for a moment a
 * band runs words of no design. Below about 120 Hz that can push the band's
 * poles outside the unit circle, and the output rails. Here a turn starts a copy
 * of the EQ whose fifteen words glide together, which keeps every band between
 * two designs, and plays it until the EQ itself has settled again.
 */
#ifndef GM2_EQ_ALT_H
#define GM2_EQ_ALT_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_base.h"
#include "gm2_eq.h"
#include "gm2_handback.h"
#include "gm2_job.h"

typedef gm2_eq_config gm2_eq_alt_config;

typedef struct {
  gm2_eq_state exact; /* the EQ, always run */
  gm2_eq_state own;   /* the de-popped copy, run from a turn until the hand-back */
  int32_t from[GM2_EQ_COEFS];
  int32_t to[GM2_EQ_COEFS];
  gm2_job_group group;
  gm2_handback handback;
} gm2_eq_alt_state;

void gm2_eq_alt_state_init(gm2_eq_alt_state *state, const gm2_eq_alt_config *config);

void gm2_eq_alt_process_split(gm2_eq_alt_state *state, const gm2_eq_alt_config *config,
                              const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                              int32_t *out_r, size_t frames);

void gm2_eq_alt_process_f32(gm2_eq_alt_state *state, const gm2_eq_alt_config *config,
                            const float *in_l, const float *in_r, float *out_l, float *out_r,
                            size_t frames);

void gm2_eq_alt_process_interleaved(gm2_eq_alt_state *state, const gm2_eq_alt_config *config,
                                    const int32_t *in, int32_t *out, size_t frames);

void gm2_eq_alt_process_frame(gm2_eq_alt_state *state, const gm2_eq_alt_config *config,
                              const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_EQ_ALT_H */
