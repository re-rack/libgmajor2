/* G-Major 2 Ping Pong Delay, Delay block subtype 0 (manual p. 47).
 *
 * It runs the shared Delay routine (gm2_delay_core.h) with both lines at the same
 * delay time (from Delay or Tempo), cross-coupled feedback (line 0 into line 1
 * and line 1 into line 0), and stereo Width panning on the output matrix.
 */
#ifndef GM2_PINGPONG_H
#define GM2_PINGPONG_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_delay_core.h"

typedef struct {
  int32_t coef[GM2_DELAY_CORE_COEFS];       /* X+0..19, the words the updaters stage */
  int32_t tap_offset[GM2_DELAY_CORE_LINES]; /* X:0x098E2A and X:0x098E35: each line's offset */
} gm2_pingpong_config;

typedef gm2_delay_core_state gm2_pingpong_state;

void gm2_pingpong_state_init(gm2_pingpong_state *state, const gm2_pingpong_config *config);

void gm2_pingpong_process_split(gm2_pingpong_state *state, const gm2_pingpong_config *config,
                                const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                                int32_t *out_r, size_t frames);

void gm2_pingpong_process_f32(gm2_pingpong_state *state, const gm2_pingpong_config *config,
                              const float *in_l, const float *in_r, float *out_l, float *out_r,
                              size_t frames);

void gm2_pingpong_process_interleaved(gm2_pingpong_state *state, const gm2_pingpong_config *config,
                                      const int32_t *in, int32_t *out, size_t frames);

void gm2_pingpong_process_frame(gm2_pingpong_state *state, const gm2_pingpong_config *config,
                                const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_PINGPONG_H */
