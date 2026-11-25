/* G-Major 2 Advanced Chorus, Modulation block subtype 1.
 *
 * It runs the shared modulated-delay routine (gm2_moddelay_core.h) with the uniform sine LFO, Delay
 * setting the voice pair's base delay and Golden Ratio scaling Depth with the LFO rate.
 */
#ifndef GM2_ACHORUS_H
#define GM2_ACHORUS_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_moddelay_core.h"

typedef gm2_moddelay_core_config gm2_achorus_config;
typedef gm2_moddelay_core_state gm2_achorus_state;

void gm2_achorus_state_init(gm2_achorus_state *state, const gm2_achorus_config *config);

void gm2_achorus_process_split(gm2_achorus_state *state, const gm2_achorus_config *config,
                               const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                               int32_t *out_r, size_t frames);

void gm2_achorus_process_f32(gm2_achorus_state *state, const gm2_achorus_config *config,
                             const float *in_l, const float *in_r, float *out_l, float *out_r,
                             size_t frames);

void gm2_achorus_process_interleaved(gm2_achorus_state *state, const gm2_achorus_config *config,
                                     const int32_t *in, int32_t *out, size_t frames);

void gm2_achorus_process_frame(gm2_achorus_state *state, const gm2_achorus_config *config,
                               const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

void gm2_achorus_align(gm2_achorus_state *state, const gm2_achorus_config *config, double beats);

#endif /* GM2_ACHORUS_H */
