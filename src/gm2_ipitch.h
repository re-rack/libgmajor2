/* G-Major 2 Intelligent Pitch Shifter, Pitch block subtype 4.
 *
 * It runs the pitch routine (gm2_pitch_core.h) with two voices, each a number of
 * scale steps from the note the detector hears, in the Key and Scale.
 */
#ifndef GM2_IPITCH_H
#define GM2_IPITCH_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_pitch_core.h"

typedef gm2_pitch_core_config gm2_ipitch_config;
typedef gm2_pitch_core_state gm2_ipitch_state;

void gm2_ipitch_state_init(gm2_ipitch_state *state, const gm2_ipitch_config *config);

void gm2_ipitch_process_split(gm2_ipitch_state *state, const gm2_ipitch_config *config,
                              const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                              int32_t *out_r, size_t frames);

void gm2_ipitch_process_f32(gm2_ipitch_state *state, const gm2_ipitch_config *config,
                            const float *in_l, const float *in_r, float *out_l, float *out_r,
                            size_t frames);

void gm2_ipitch_process_interleaved(gm2_ipitch_state *state, const gm2_ipitch_config *config,
                                    const int32_t *in, int32_t *out, size_t frames);

void gm2_ipitch_process_frame(gm2_ipitch_state *state, const gm2_ipitch_config *config,
                              const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_IPITCH_H */
