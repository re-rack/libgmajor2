/* G-Major 2 Through-Zero Flanger Positive, Modulation block subtype 7.
 *
 * It runs the shared modulated-delay routine (gm2_moddelay_core.h) with a fixed four-sample base
 * delay and the positive variant's equal voice gains.
 */
#ifndef GM2_TZFLANGER_POS_H
#define GM2_TZFLANGER_POS_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_moddelay_core.h"

typedef gm2_moddelay_core_config gm2_tzflanger_pos_config;
typedef gm2_moddelay_core_state gm2_tzflanger_pos_state;

void gm2_tzflanger_pos_state_init(gm2_tzflanger_pos_state *state,
                                  const gm2_tzflanger_pos_config *config);

void gm2_tzflanger_pos_process_split(gm2_tzflanger_pos_state *state,
                                     const gm2_tzflanger_pos_config *config, const int32_t *in_l,
                                     const int32_t *in_r, int32_t *out_l, int32_t *out_r,
                                     size_t frames);

void gm2_tzflanger_pos_process_f32(gm2_tzflanger_pos_state *state,
                                   const gm2_tzflanger_pos_config *config, const float *in_l,
                                   const float *in_r, float *out_l, float *out_r, size_t frames);

void gm2_tzflanger_pos_process_interleaved(gm2_tzflanger_pos_state *state,
                                           const gm2_tzflanger_pos_config *config,
                                           const int32_t *in, int32_t *out, size_t frames);

void gm2_tzflanger_pos_process_frame(gm2_tzflanger_pos_state *state,
                                     const gm2_tzflanger_pos_config *config,
                                     const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

void gm2_tzflanger_pos_align(gm2_tzflanger_pos_state *state, const gm2_tzflanger_pos_config *config,
                             double beats);

#endif /* GM2_TZFLANGER_POS_H */
