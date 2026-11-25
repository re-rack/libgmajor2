/* G-Major 2 three-band EQ, in the Gate & EQ block (algorithm 6): the kernel
 * P:0x000CC0..0x000D14.
 *
 * Per stereo frame the kernel multiplies both input words by the block's input
 * gain, runs each channel through three biquads in series (gm2_biquad.h) and
 * applies the block's wet word.
 *
 * Each band's five words ride a one-pole job (gm2_job.h).
 */
#ifndef GM2_EQ_H
#define GM2_EQ_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_base.h"
#include "gm2_biquad.h"

#define GM2_EQ_BANDS 3u
#define GM2_EQ_COEFS (GM2_BIQUAD_COEFS * GM2_EQ_BANDS)

typedef struct {
  /* Y:0x385..0x393, staged at X:0x098771..0x09877F by P:0x0906B6: five biquad
   * words a band */
  int32_t coef[GM2_EQ_COEFS];
} gm2_eq_config;

typedef struct {
  int32_t in[GM2_CHANNELS][2]; /* X:0x50/0x51 and X:0x58/0x59, x[n-1] and x[n-2] */
  gm2_biquad band[GM2_CHANNELS][GM2_EQ_BANDS]; /* L:0x52..0x57 and L:0x5A..0x5F */
  int32_t coef[GM2_EQ_COEFS]; /* the live words, gliding to the config's */
  unsigned chunk_pos; /* frame within the chunk */
} gm2_eq_state;

void gm2_eq_state_init(gm2_eq_state *state, const gm2_eq_config *config);

void gm2_eq_process_split(gm2_eq_state *state, const gm2_eq_config *config, const int32_t *in_l,
                          const int32_t *in_r, int32_t *out_l, int32_t *out_r, size_t frames);

void gm2_eq_process_f32(gm2_eq_state *state, const gm2_eq_config *config, const float *in_l,
                        const float *in_r, float *out_l, float *out_r, size_t frames);

void gm2_eq_process_interleaved(gm2_eq_state *state, const gm2_eq_config *config, const int32_t *in,
                                int32_t *out, size_t frames);

void gm2_eq_process_frame(gm2_eq_state *state, const gm2_eq_config *config,
                          const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_EQ_H */
