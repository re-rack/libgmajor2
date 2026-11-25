/* G-Major 2 Tremolo, Filter block subtype 5.
 *
 * It runs the Tremolo/Panner routine (gm2_lfo_gain_core.h) with a Soft (triangle)
 * or Hard (square) LFO, the same gain on both channels and its Hi-Cut.
 */
#ifndef GM2_TREMOLO_H
#define GM2_TREMOLO_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_lfo_gain_core.h"

typedef gm2_lfo_gain_core_config gm2_tremolo_config;
typedef gm2_lfo_gain_core_state gm2_tremolo_state;

void gm2_tremolo_state_init(gm2_tremolo_state *state, const gm2_tremolo_config *config);

void gm2_tremolo_process_split(gm2_tremolo_state *state, const gm2_tremolo_config *config,
                               const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                               int32_t *out_r, size_t frames);

void gm2_tremolo_process_f32(gm2_tremolo_state *state, const gm2_tremolo_config *config,
                             const float *in_l, const float *in_r, float *out_l, float *out_r,
                             size_t frames);

void gm2_tremolo_process_interleaved(gm2_tremolo_state *state, const gm2_tremolo_config *config,
                                     const int32_t *in, int32_t *out, size_t frames);

void gm2_tremolo_process_frame(gm2_tremolo_state *state, const gm2_tremolo_config *config,
                               const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

void gm2_tremolo_align(gm2_tremolo_state *state, const gm2_tremolo_config *config, double beats);

#endif /* GM2_TREMOLO_H */
