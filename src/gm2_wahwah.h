/* G-Major 2 Wah Wah, Filter block subtype 0.
 *
 * It runs the wah routine (gm2_wah_core.h) at one frequency: the device never
 * moves subtype 0's sweep from its background loop, so the Sweep word stays where
 * the parameter puts it and Range picks which of the two shift words P:0x092527
 * writes.
 */
#ifndef GM2_WAHWAH_H
#define GM2_WAHWAH_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_wah_core.h"

typedef struct {
  int32_t shift; /* Y:0x40A: the filter's frequency shift, from Range */
  int32_t sweep; /* Y:0x40E: the word the routine's cubic reads */
  int32_t wet;   /* Y:0x41A: the Mix knob's wet and dry words */
  int32_t dry;   /* Y:0x41B */
} gm2_wahwah_config;

typedef gm2_wah_core_state gm2_wahwah_state;

void gm2_wahwah_state_init(gm2_wahwah_state *state, const gm2_wahwah_config *config);

void gm2_wahwah_process_split(gm2_wahwah_state *state, const gm2_wahwah_config *config,
                              const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                              int32_t *out_r, size_t frames);

void gm2_wahwah_process_f32(gm2_wahwah_state *state, const gm2_wahwah_config *config,
                            const float *in_l, const float *in_r, float *out_l, float *out_r,
                            size_t frames);

void gm2_wahwah_process_interleaved(gm2_wahwah_state *state, const gm2_wahwah_config *config,
                                    const int32_t *in, int32_t *out, size_t frames);

void gm2_wahwah_process_frame(gm2_wahwah_state *state, const gm2_wahwah_config *config,
                              const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_WAHWAH_H */
