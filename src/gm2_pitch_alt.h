/* The de-popped Pitch Shifter: gm2_pitch.h with a turn's splice repair handed back
 * to the shifter's own path once settled (gm2_pitch_core_alt.h).
 */
#ifndef GM2_PITCH_ALT_H
#define GM2_PITCH_ALT_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_base.h"
#include "gm2_pitch.h"
#include "gm2_pitch_core_alt.h"

typedef gm2_pitch_config gm2_pitch_alt_config;
typedef gm2_pitch_core_alt_state gm2_pitch_alt_state;

void gm2_pitch_alt_state_init(gm2_pitch_alt_state *state, const gm2_pitch_alt_config *config);

void gm2_pitch_alt_process_split(gm2_pitch_alt_state *state, const gm2_pitch_alt_config *config,
                                 const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                                 int32_t *out_r, size_t frames);

void gm2_pitch_alt_process_f32(gm2_pitch_alt_state *state, const gm2_pitch_alt_config *config,
                               const float *in_l, const float *in_r, float *out_l, float *out_r,
                               size_t frames);

void gm2_pitch_alt_process_interleaved(gm2_pitch_alt_state *state,
                                       const gm2_pitch_alt_config *config, const int32_t *in,
                                       int32_t *out, size_t frames);

void gm2_pitch_alt_process_frame(gm2_pitch_alt_state *state, const gm2_pitch_alt_config *config,
                                 const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_PITCH_ALT_H */
