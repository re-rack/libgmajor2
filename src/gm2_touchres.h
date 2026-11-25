/* G-Major 2 Touch Res Filter, Filter block subtype 4.
 *
 * It runs the resonance routine (gm2_res_core.h) with the coefficient set its tick
 * designs from the input's level: the level word comes from the Noise Gate's
 * detector run on the same input, a follower smooths it with the Sense and
 * Response words, and the follower sets the design's frequency between 10^2.5 Hz
 * and FrqMax. The tick runs every gm2_tick_chunks() chunks of four frames.
 */
#ifndef GM2_TOUCHRES_H
#define GM2_TOUCHRES_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_base.h"
#include "gm2_gate_detector.h"
#include "gm2_rate.h"
#include "gm2_res_core.h"
#include "gm2_res_design.h"
#include "gm2_swf.h"

typedef struct {
  int32_t coef[GM2_RES_CORE_COEFS]; /* the words at rest: a tick at level 0 */
  gm2_rate_words rate;              /* what the tick's design reads */
  gm2_res_design_tick_words words;  /* what the tick reads */
  int style;                        /* 0: 2nd order, 1: 4th order */
  unsigned tick_chunks;             /* chunks per tick, gm2_tick_chunks() */
} gm2_touchres_config;

typedef struct {
  gm2_res_core_state core;
  int32_t coef[GM2_RES_CORE_COEFS]; /* X:0x098928..0x09893B, the last tick's design */
  gm2_swf follower;                 /* UI+36/37: the follower's level, 0 at rest */
  gm2_gate_detector detector;       /* the level detector on the effect's own input */
  unsigned tick;                    /* chunks since the last tick */
} gm2_touchres_state;

void gm2_touchres_state_init(gm2_touchres_state *state, const gm2_touchres_config *config);

void gm2_touchres_process_split(gm2_touchres_state *state, const gm2_touchres_config *config,
                                const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                                int32_t *out_r, size_t frames);

void gm2_touchres_process_f32(gm2_touchres_state *state, const gm2_touchres_config *config,
                              const float *in_l, const float *in_r, float *out_l, float *out_r,
                              size_t frames);

void gm2_touchres_process_interleaved(gm2_touchres_state *state, const gm2_touchres_config *config,
                                      const int32_t *in, int32_t *out, size_t frames);

void gm2_touchres_process_frame(gm2_touchres_state *state, const gm2_touchres_config *config,
                                const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_TOUCHRES_H */
