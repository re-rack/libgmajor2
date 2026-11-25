/* G-Major 2 Vintage Phaser, Modulation block subtype 9. It runs the shared phaser
 * routine (gm2_phaser_core.h) and takes its outputs from the all-pass chains'
 * first taps, after four stages. */
#ifndef GM2_VINTPHASER_H
#define GM2_VINTPHASER_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_phaser_core.h"

typedef gm2_phaser_core_config gm2_vintphaser_config;
typedef gm2_phaser_core_state gm2_vintphaser_state;

void gm2_vintphaser_state_init(gm2_vintphaser_state *state, const gm2_vintphaser_config *config);

void gm2_vintphaser_process_split(gm2_vintphaser_state *state, const gm2_vintphaser_config *config,
                                  const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                                  int32_t *out_r, size_t frames);

void gm2_vintphaser_process_f32(gm2_vintphaser_state *state, const gm2_vintphaser_config *config,
                                const float *in_l, const float *in_r, float *out_l, float *out_r,
                                size_t frames);

void gm2_vintphaser_process_interleaved(gm2_vintphaser_state *state,
                                        const gm2_vintphaser_config *config, const int32_t *in,
                                        int32_t *out, size_t frames);

void gm2_vintphaser_process_frame(gm2_vintphaser_state *state, const gm2_vintphaser_config *config,
                                  const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

void gm2_vintphaser_align(gm2_vintphaser_state *state, const gm2_vintphaser_config *config,
                          double beats);

#endif /* GM2_VINTPHASER_H */
