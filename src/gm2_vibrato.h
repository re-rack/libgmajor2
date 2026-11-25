/* G-Major 2 Vibrato, Modulation block subtype 8.
 *
 * It runs the shared modulated-delay routine (gm2_moddelay_core.h) with the uniform sine LFO, a
 * fixed 10 ms delay, Depth scaled to a fifth and its phase-reversing output biquad.
 */
#ifndef GM2_VIBRATO_H
#define GM2_VIBRATO_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_moddelay_core.h"

typedef gm2_moddelay_core_config gm2_vibrato_config;
typedef gm2_moddelay_core_state gm2_vibrato_state;

void gm2_vibrato_state_init(gm2_vibrato_state *state, const gm2_vibrato_config *config);

void gm2_vibrato_process_split(gm2_vibrato_state *state, const gm2_vibrato_config *config,
                               const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                               int32_t *out_r, size_t frames);

void gm2_vibrato_process_f32(gm2_vibrato_state *state, const gm2_vibrato_config *config,
                             const float *in_l, const float *in_r, float *out_l, float *out_r,
                             size_t frames);

void gm2_vibrato_process_interleaved(gm2_vibrato_state *state, const gm2_vibrato_config *config,
                                     const int32_t *in, int32_t *out, size_t frames);

void gm2_vibrato_process_frame(gm2_vibrato_state *state, const gm2_vibrato_config *config,
                               const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

void gm2_vibrato_align(gm2_vibrato_state *state, const gm2_vibrato_config *config, double beats);

#endif /* GM2_VIBRATO_H */
