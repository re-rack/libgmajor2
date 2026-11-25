/* G-Major 2 Tri-Chorus Normal, Modulation block subtype 4.
 *
 * It runs the shared modulated-delay routine (gm2_moddelay_core.h) with the Tri-Chorus Normal phase
 * offsets and Width setting each voice's base delay.
 */
#ifndef GM2_TRICHORUS_H
#define GM2_TRICHORUS_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_moddelay_core.h"

typedef gm2_moddelay_core_config gm2_trichorus_config;
typedef gm2_moddelay_core_state gm2_trichorus_state;

void gm2_trichorus_state_init(gm2_trichorus_state *state, const gm2_trichorus_config *config);

void gm2_trichorus_process_split(gm2_trichorus_state *state, const gm2_trichorus_config *config,
                                 const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                                 int32_t *out_r, size_t frames);

void gm2_trichorus_process_f32(gm2_trichorus_state *state, const gm2_trichorus_config *config,
                               const float *in_l, const float *in_r, float *out_l, float *out_r,
                               size_t frames);

void gm2_trichorus_process_interleaved(gm2_trichorus_state *state,
                                       const gm2_trichorus_config *config, const int32_t *in,
                                       int32_t *out, size_t frames);

void gm2_trichorus_process_frame(gm2_trichorus_state *state, const gm2_trichorus_config *config,
                                 const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

void gm2_trichorus_align(gm2_trichorus_state *state, const gm2_trichorus_config *config,
                         double beats);

#endif /* GM2_TRICHORUS_H */
