/* G-Major 2 Advanced Flanger, Modulation block subtype 3.
 *
 * It runs the shared modulated-delay routine (gm2_moddelay_core.h) with the uniform sine LFO, Delay
 * and Golden Ratio on its feedback delay, and Phase Reverse.
 */
#ifndef GM2_AFLANGER_H
#define GM2_AFLANGER_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_moddelay_core.h"

typedef gm2_moddelay_core_config gm2_aflanger_config;
typedef gm2_moddelay_core_state gm2_aflanger_state;

void gm2_aflanger_state_init(gm2_aflanger_state *state, const gm2_aflanger_config *config);

void gm2_aflanger_process_split(gm2_aflanger_state *state, const gm2_aflanger_config *config,
                                const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                                int32_t *out_r, size_t frames);

void gm2_aflanger_process_f32(gm2_aflanger_state *state, const gm2_aflanger_config *config,
                              const float *in_l, const float *in_r, float *out_l, float *out_r,
                              size_t frames);

void gm2_aflanger_process_interleaved(gm2_aflanger_state *state, const gm2_aflanger_config *config,
                                      const int32_t *in, int32_t *out, size_t frames);

void gm2_aflanger_process_frame(gm2_aflanger_state *state, const gm2_aflanger_config *config,
                                const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

void gm2_aflanger_align(gm2_aflanger_state *state, const gm2_aflanger_config *config, double beats);

#endif /* GM2_AFLANGER_H */
