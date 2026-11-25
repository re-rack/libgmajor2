/* G-Major 2 Panner, Filter block subtype 6.
 *
 * It runs the Tremolo/Panner routine (gm2_lfo_gain_core.h) with the sine LFO, the
 * right channel half a cycle behind the left, and the Hi-Cut open.
 */
#ifndef GM2_PANNER_H
#define GM2_PANNER_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_lfo_gain_core.h"

typedef gm2_lfo_gain_core_config gm2_panner_config;
typedef gm2_lfo_gain_core_state gm2_panner_state;

void gm2_panner_state_init(gm2_panner_state *state, const gm2_panner_config *config);

void gm2_panner_process_split(gm2_panner_state *state, const gm2_panner_config *config,
                              const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                              int32_t *out_r, size_t frames);

void gm2_panner_process_f32(gm2_panner_state *state, const gm2_panner_config *config,
                            const float *in_l, const float *in_r, float *out_l, float *out_r,
                            size_t frames);

void gm2_panner_process_interleaved(gm2_panner_state *state, const gm2_panner_config *config,
                                    const int32_t *in, int32_t *out, size_t frames);

void gm2_panner_process_frame(gm2_panner_state *state, const gm2_panner_config *config,
                              const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

void gm2_panner_align(gm2_panner_state *state, const gm2_panner_config *config, double beats);

#endif /* GM2_PANNER_H */
