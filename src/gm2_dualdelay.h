/* G-Major 2 Dual Delay, Delay block subtype 2 (manual p. 48).
 *
 * It runs the shared Delay routine (gm2_delay_core.h) as two independent delay
 * lines (left into line 0, right into line 1), each with its own delay time,
 * tempo subdivision, feedback and output pan, and shared feedback Hi-Cut and
 * Lo-Cut filters.
 */
#ifndef GM2_DUALDELAY_H
#define GM2_DUALDELAY_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_delay_core.h"

typedef struct {
  int32_t coef[GM2_DELAY_CORE_COEFS];       /* X+0..19, the words the updaters stage */
  int32_t tap_offset[GM2_DELAY_CORE_LINES]; /* X:0x098E2A and X:0x098E35: each line's offset */
} gm2_dualdelay_config;

typedef gm2_delay_core_state gm2_dualdelay_state;

void gm2_dualdelay_state_init(gm2_dualdelay_state *state, const gm2_dualdelay_config *config);

void gm2_dualdelay_process_split(gm2_dualdelay_state *state, const gm2_dualdelay_config *config,
                                 const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                                 int32_t *out_r, size_t frames);

void gm2_dualdelay_process_f32(gm2_dualdelay_state *state, const gm2_dualdelay_config *config,
                               const float *in_l, const float *in_r, float *out_l, float *out_r,
                               size_t frames);

void gm2_dualdelay_process_interleaved(gm2_dualdelay_state *state,
                                       const gm2_dualdelay_config *config, const int32_t *in,
                                       int32_t *out, size_t frames);

void gm2_dualdelay_process_frame(gm2_dualdelay_state *state, const gm2_dualdelay_config *config,
                                 const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_DUALDELAY_H */
