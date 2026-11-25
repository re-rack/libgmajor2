/* G-Major 2 Whammy, Pitch block subtype 1.
 *
 * It runs the pitch routine (gm2_pitch_core.h) with one voice on both channels,
 * whose pitch the Pitch value bends up or down by as much as the Range.
 */
#ifndef GM2_WHAMMY_H
#define GM2_WHAMMY_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_pitch_core.h"

typedef gm2_pitch_core_config gm2_whammy_config;
typedef gm2_pitch_core_state gm2_whammy_state;

void gm2_whammy_state_init(gm2_whammy_state *state, const gm2_whammy_config *config);

void gm2_whammy_process_split(gm2_whammy_state *state, const gm2_whammy_config *config,
                              const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                              int32_t *out_r, size_t frames);

void gm2_whammy_process_f32(gm2_whammy_state *state, const gm2_whammy_config *config,
                            const float *in_l, const float *in_r, float *out_l, float *out_r,
                            size_t frames);

void gm2_whammy_process_interleaved(gm2_whammy_state *state, const gm2_whammy_config *config,
                                    const int32_t *in, int32_t *out, size_t frames);

void gm2_whammy_process_frame(gm2_whammy_state *state, const gm2_whammy_config *config,
                              const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_WHAMMY_H */
