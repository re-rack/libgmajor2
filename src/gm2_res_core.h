/* The resonance filter routine P:0x000EE9..0x000F23, which the Resonance Filter
 * and the Touch Res Filter run (Filter block subtypes 3 and 4).
 *
 * Per stereo frame the routine multiplies both input words by the block's input
 * gain and shifts them down three bits, runs each channel through two biquads in
 * series (gm2_biquad.h), then the soft clip P:0x0003AF with its shift back up,
 * and blends the result with the frame's input sample through the Mix knob's wet
 * and dry words.
 *
 * The twenty biquad words ride a one-pole job (gm2_job.h) towards the words the
 * effect hands the routine, and the two level words ride one of their own.
 */
#ifndef GM2_RES_CORE_H
#define GM2_RES_CORE_H

#include <stdint.h>

#include "gm2_base.h"
#include "gm2_biquad.h"

#define GM2_RES_CORE_BIQUADS 2u
/* Five words a biquad, the left channel's two biquads first. */
#define GM2_RES_CORE_COEFS (GM2_BIQUAD_COEFS * GM2_RES_CORE_BIQUADS * GM2_CHANNELS)

typedef struct {
  /* X/L:0x060..0x06B: each channel's x[n-1] and x[n-2], then its two biquads */
  int32_t in[GM2_CHANNELS][2];
  gm2_biquad biquad[GM2_CHANNELS][GM2_RES_CORE_BIQUADS];
  int32_t coef[GM2_RES_CORE_COEFS]; /* Y:0x3DA..0x3ED, the live words */
  int32_t wet;                      /* Y:0x3EF: the live level words, Y:0x3F0 the dry one */
  int32_t dry;
  unsigned chunk_pos; /* frame within the chunk */
} gm2_res_core_state;

/* Silent biquads on `coef`, the level words on `wet` and `dry`. */
void gm2_res_core_state_init(gm2_res_core_state *state, const int32_t coef[GM2_RES_CORE_COEFS],
                             int32_t wet, int32_t dry);

/* One stereo frame, then the chunk's job run once its last frame is done: the
 * live words move towards `coef`, the words the design staged at
 * X:0x098928..0x09893B, and the live level words towards `wet` and `dry`.
 * Returns 1 when the frame ended a chunk and 0 otherwise, for an effect that acts
 * between chunks. */
int gm2_res_core_process_frame(gm2_res_core_state *state, const int32_t coef[GM2_RES_CORE_COEFS],
                               int32_t wet, int32_t dry, const int32_t in[GM2_CHANNELS],
                               int32_t out[GM2_CHANNELS]);

#endif /* GM2_RES_CORE_H */
