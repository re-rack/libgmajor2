/* G-Major 2 Touch Wah, Filter block subtype 1.
 *
 * It runs the wah routine (gm2_wah_core.h) and answers the device's control tick:
 * once every gm2_tick.h period the peak hold of the routine's envelope becomes a
 * level, a follower walks towards it, and Sense turns the follower into the sweep
 * word the filter moves to. Response sets how far one tick walks: about a fifth,
 * a third or a half of the gap for Slow, Medium and Fast.
 *
 * The envelope, its peak hold, the follower and the tick belong to this effect.
 */
#ifndef GM2_TOUCHWAH_H
#define GM2_TOUCHWAH_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_swf.h"
#include "gm2_wah_core.h"

typedef struct {
  int32_t shift;            /* Y:0x40A: the filter's frequency shift, from Range */
  int32_t decay;            /* Y:0x415: the envelope's decay pole */
  gm2_swf follow_coeff;     /* UI+46/47: the share of the gap one tick takes */
  gm2_swf follow_threshold; /* UI+48/49: the gap above which it takes that share */
  gm2_swf centre;           /* UI+52/53: the follower level that reads as zero */
  gm2_swf span;             /* UI+54/55: the sweep word per unit of follower level */
  int32_t wet;              /* Y:0x41A: the Mix knob's wet and dry words */
  int32_t dry;              /* Y:0x41B */
  unsigned tick_chunks;     /* chunks per tick, gm2_tick_chunks() */
} gm2_touchwah_config;

typedef struct {
  gm2_wah_core_state core;
  int32_t envelope; /* X:0x2C0 */
  int32_t peak;     /* Y:0x418: the envelope's log2 peak hold */
  gm2_swf follower; /* UI+50/51: the follower's level, 0 at rest */
  unsigned tick;    /* chunks since the last tick */
} gm2_touchwah_state;

void gm2_touchwah_state_init(gm2_touchwah_state *state, const gm2_touchwah_config *config);

void gm2_touchwah_process_split(gm2_touchwah_state *state, const gm2_touchwah_config *config,
                                const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                                int32_t *out_r, size_t frames);

void gm2_touchwah_process_f32(gm2_touchwah_state *state, const gm2_touchwah_config *config,
                              const float *in_l, const float *in_r, float *out_l, float *out_r,
                              size_t frames);

void gm2_touchwah_process_interleaved(gm2_touchwah_state *state, const gm2_touchwah_config *config,
                                      const int32_t *in, int32_t *out, size_t frames);

void gm2_touchwah_process_frame(gm2_touchwah_state *state, const gm2_touchwah_config *config,
                                const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_TOUCHWAH_H */
