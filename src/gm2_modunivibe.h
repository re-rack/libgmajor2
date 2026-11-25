/* G-Major 2 Modern Univibe, Modulation block subtype 12. It runs the Univibe
 * routine (gm2_univibe_core.h), whose waveshaper adds a linear term to the
 * squared lamp drive and bends it back.
 */
#ifndef GM2_MODUNIVIBE_H
#define GM2_MODUNIVIBE_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_univibe_core.h"

typedef gm2_univibe_core_config gm2_modunivibe_config;
typedef gm2_univibe_core_state gm2_modunivibe_state;

void gm2_modunivibe_state_init(gm2_modunivibe_state *state, const gm2_modunivibe_config *config);

void gm2_modunivibe_process_split(gm2_modunivibe_state *state, const gm2_modunivibe_config *config,
                                  const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                                  int32_t *out_r, size_t frames);

void gm2_modunivibe_process_f32(gm2_modunivibe_state *state, const gm2_modunivibe_config *config,
                                const float *in_l, const float *in_r, float *out_l, float *out_r,
                                size_t frames);

void gm2_modunivibe_process_interleaved(gm2_modunivibe_state *state,
                                        const gm2_modunivibe_config *config, const int32_t *in,
                                        int32_t *out, size_t frames);

void gm2_modunivibe_process_frame(gm2_modunivibe_state *state, const gm2_modunivibe_config *config,
                                  const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

void gm2_modunivibe_align(gm2_modunivibe_state *state, const gm2_modunivibe_config *config,
                          double beats);

#endif /* GM2_MODUNIVIBE_H */
