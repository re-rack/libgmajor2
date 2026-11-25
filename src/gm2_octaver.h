/* G-Major 2 Octaver, Pitch block subtype 2.
 *
 * It runs the pitch routine (gm2_pitch_core.h) with one voice on both channels, one
 * or two octaves up or down.
 */
#ifndef GM2_OCTAVER_H
#define GM2_OCTAVER_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_pitch_core.h"

typedef gm2_pitch_core_config gm2_octaver_config;
typedef gm2_pitch_core_state gm2_octaver_state;

void gm2_octaver_state_init(gm2_octaver_state *state, const gm2_octaver_config *config);

void gm2_octaver_process_split(gm2_octaver_state *state, const gm2_octaver_config *config,
                               const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                               int32_t *out_r, size_t frames);

void gm2_octaver_process_f32(gm2_octaver_state *state, const gm2_octaver_config *config,
                             const float *in_l, const float *in_r, float *out_l, float *out_r,
                             size_t frames);

void gm2_octaver_process_interleaved(gm2_octaver_state *state, const gm2_octaver_config *config,
                                     const int32_t *in, int32_t *out, size_t frames);

void gm2_octaver_process_frame(gm2_octaver_state *state, const gm2_octaver_config *config,
                               const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_OCTAVER_H */
