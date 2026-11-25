/* G-Major 2 Resonance Filter, Filter block subtype 3.
 *
 * It runs the resonance routine (gm2_res_core.h) with the coefficient set
 * gm2_res_design.h computes from its Style, Hi-Cut and Resonance: Style 0 puts a
 * second-order design in the first biquad and leaves the second at the fixed set,
 * Style 1 repeats the design in both. The twenty words glide to a new set through
 * the routine's one-pole job.
 */
#ifndef GM2_RESFILTER_H
#define GM2_RESFILTER_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_base.h"
#include "gm2_res_core.h"

typedef struct {
  int32_t coef[GM2_RES_CORE_COEFS]; /* X:0x098928..0x09893B, the words the design staged */
} gm2_resfilter_config;

typedef gm2_res_core_state gm2_resfilter_state;

void gm2_resfilter_state_init(gm2_resfilter_state *state, const gm2_resfilter_config *config);

void gm2_resfilter_process_split(gm2_resfilter_state *state, const gm2_resfilter_config *config,
                                 const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                                 int32_t *out_r, size_t frames);

void gm2_resfilter_process_f32(gm2_resfilter_state *state, const gm2_resfilter_config *config,
                               const float *in_l, const float *in_r, float *out_l, float *out_r,
                               size_t frames);

void gm2_resfilter_process_interleaved(gm2_resfilter_state *state,
                                       const gm2_resfilter_config *config, const int32_t *in,
                                       int32_t *out, size_t frames);

void gm2_resfilter_process_frame(gm2_resfilter_state *state, const gm2_resfilter_config *config,
                                 const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_RESFILTER_H */
