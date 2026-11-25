/* G-Major 2 Auto Wah, Filter block subtype 2.
 *
 * It runs the wah routine (gm2_wah_core.h) and answers the device's control tick:
 * once every gm2_tick.h period the sweep word takes the LFO's output, so the
 * filter sweeps at the Speed or Tempo rate.
 *
 * The LFO, the job that glides its step and the tick belong to this effect.
 */
#ifndef GM2_AUTOWAH_H
#define GM2_AUTOWAH_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_wah_core.h"

typedef struct {
  int32_t shift;        /* Y:0x40A: the filter's frequency shift, from Range */
  int32_t shape;        /* Y:0x414: 0 for the triangle, 1 for the sine */
  int32_t lfo_step;     /* X:0x09899F, staged for Y:0x413: the phase step per chunk */
  int32_t wet;          /* Y:0x41A: the Mix knob's wet and dry words */
  int32_t dry;          /* Y:0x41B */
  unsigned note;        /* the Tempo value the step follows, 0 for Speed */
  unsigned tick_chunks; /* chunks per tick, gm2_tick_chunks() */
} gm2_autowah_config;

typedef struct {
  gm2_wah_core_state core;
  int32_t lfo_phase;  /* X:0x2BC */
  int32_t lfo_smooth; /* X:0x2BD: the triangle's one-pole */
  int32_t lfo_out;    /* X:0x2BE */
  int32_t lfo_step;   /* Y:0x413: the live step, gliding to the config's */
  unsigned tick;      /* chunks since the last tick */
} gm2_autowah_state;

void gm2_autowah_state_init(gm2_autowah_state *state, const gm2_autowah_config *config);

void gm2_autowah_process_split(gm2_autowah_state *state, const gm2_autowah_config *config,
                               const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                               int32_t *out_r, size_t frames);

void gm2_autowah_process_f32(gm2_autowah_state *state, const gm2_autowah_config *config,
                             const float *in_l, const float *in_r, float *out_l, float *out_r,
                             size_t frames);

void gm2_autowah_process_interleaved(gm2_autowah_state *state, const gm2_autowah_config *config,
                                     const int32_t *in, int32_t *out, size_t frames);

void gm2_autowah_process_frame(gm2_autowah_state *state, const gm2_autowah_config *config,
                               const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

void gm2_autowah_align(gm2_autowah_state *state, const gm2_autowah_config *config, double beats);

#endif /* GM2_AUTOWAH_H */
