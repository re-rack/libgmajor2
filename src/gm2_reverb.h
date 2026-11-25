/* G-Major 2 Reverb (algorithm 5): the kernel P:0x0012D2..0x0013DE.
 *
 * Spring, Hall, Room and Plate are one algorithm at four factory settings.
 *
 * Per stereo frame the kernel writes the mono input to a 32,768-word buffer. Two
 * pointers then walk the buffer from that write point, in turns and each by the
 * next of forty-one offsets. Along the walk the pre-delayed input runs through a
 * one-pole and two all-pass diffusers into seven tank lines, each with a damping
 * filter of two bands; the lines' outputs, crossed by a matrix of signs, give the
 * tank's two sums. Each channel then taps eight early reflections in three
 * groups, each group with a comb feedback, and runs their sum through two
 * biquads. The Room and Reverb levels mix the early and the tank signal, and the
 * Mix knob's wet and dry words blend that sum with the frame's input.
 *
 * The eighty words below ride one-pole jobs (gm2_job.h). The offsets, the signs
 * and the early reflections' weights are written straight to the words the
 * kernel reads.
 */
#ifndef GM2_REVERB_H
#define GM2_REVERB_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_base.h"

#define GM2_REVERB_BUFFER_WORDS 0x008000u /* Y:0x0A0000..0x0A7FFF, m5 = m7 = 0x7FFF */
#define GM2_REVERB_TANK_LINES 7u
#define GM2_REVERB_TANK_SIGNS (2u * GM2_REVERB_TANK_LINES)
#define GM2_REVERB_EARLY_TAPS 8u
#define GM2_REVERB_EARLY_COMBS 3u
#define GM2_REVERB_EARLY_BIQUADS 2u
#define GM2_REVERB_OFFSETS 42u
#define GM2_REVERB_COEFS 80u

/* The positions in coef[], the order of the staged words X:0x098F64..0x098FB3. */
enum {
  GM2_REVERB_INPUT_LP = 0,  /* Y:0x0589..0x058B: the input's one-pole */
  GM2_REVERB_DIFFUSER = 3,  /* Y:0x058C..0x058F: the two diffusers' gain and its complement */
  GM2_REVERB_DAMPING = 7,   /* Y:0x0590..0x05B9: six words a tank line */
  GM2_REVERB_COMB = 49,     /* Y:0x05BB..0x05BD left, Y:0x05C8..0x05CA right */
  GM2_REVERB_BIQUAD = 55,   /* Y:0x05BE..0x05C7 left, Y:0x05CB..0x05D4 right: five words each */
  GM2_REVERB_ROOM = 75,     /* Y:0x05D5/0x05D6: the early signal's level, right and left */
  GM2_REVERB_LEVEL = 77,    /* Y:0x05D7/0x05D8: the tank signal's level, right and left */
  GM2_REVERB_PRE_DELAY = 79, /* Y:0x05D9: the pre-delay tap's gain */
};

typedef struct {
  int32_t coef[GM2_REVERB_COEFS]; /* the words the updaters stage */
  int32_t wet;                    /* X:0x099047: the Mix knob's wet word */
  int32_t dry;                    /* X:0x099048: and its dry word */
  /* X:0x037E..0x03A7: the steps the two pointers take, in the order they take
   * them, and last the pre-delay tap's distance from the write point */
  int32_t offsets[GM2_REVERB_OFFSETS];
  int32_t tank_signs[GM2_REVERB_TANK_SIGNS]; /* X:0x03BC..0x03C9: the tank's matrix */
  /* X:0x03E7..0x03EE and X:0x03F1..0x03F8: each channel's early reflection
   * weights */
  int32_t early[GM2_CHANNELS][GM2_REVERB_EARLY_TAPS];
} gm2_reverb_config;

/* One early-reflection biquad. */
typedef struct {
  int32_t in[2];  /* x[n-2], x[n-1] */
  int32_t out[2]; /* y[n-2], y[n-1] */
  int32_t low;    /* the fraction of y[n-1] its accumulator held below the word */
} gm2_reverb_biquad;

typedef struct {
  int32_t buffer[GM2_REVERB_BUFFER_WORDS];
  uint32_t write;                                /* X:0x037D, less the buffer's base */
  int32_t pre_delay;                             /* X:0x03A8: the pre-delay tap, scaled */
  int32_t input_lp;                              /* X:0x03AD: the input one-pole's state */
  int32_t damping[GM2_REVERB_TANK_LINES][2];     /* X:0x03AE..0x03BB: each line's two bands */
  int32_t tank_out[2];                           /* X:0x03A9/0x03AA: the tank's two sums */
  int32_t comb[GM2_CHANNELS][GM2_REVERB_EARLY_COMBS]; /* X:0x03CA.. left, X:0x03D5.. right */
  /* X:0x03CD..0x03D4 and X:0x03EF/0x03F0 left, X:0x03D8..0x03DF and
   * X:0x03F9/0x03FA right */
  gm2_reverb_biquad biquad[GM2_CHANNELS][GM2_REVERB_EARLY_BIQUADS];
  int32_t early_out[GM2_CHANNELS]; /* X:0x03AB/0x03AC: each channel's early signal */
  int32_t coef[GM2_REVERB_COEFS];  /* the live words, gliding to the config's */
  int32_t wet;                     /* Y:0x05DB: the same for the Mix pair */
  int32_t dry;                     /* Y:0x05DC */
  unsigned chunk_pos;              /* frame within the chunk */
} gm2_reverb_state;

void gm2_reverb_state_init(gm2_reverb_state *state, const gm2_reverb_config *config);

void gm2_reverb_process_split(gm2_reverb_state *state, const gm2_reverb_config *config,
                              const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                              int32_t *out_r, size_t frames);

void gm2_reverb_process_f32(gm2_reverb_state *state, const gm2_reverb_config *config,
                            const float *in_l, const float *in_r, float *out_l, float *out_r,
                            size_t frames);

void gm2_reverb_process_interleaved(gm2_reverb_state *state, const gm2_reverb_config *config,
                                    const int32_t *in, int32_t *out, size_t frames);

void gm2_reverb_process_frame(gm2_reverb_state *state, const gm2_reverb_config *config,
                              const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_REVERB_H */
