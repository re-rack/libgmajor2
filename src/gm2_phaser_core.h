/* The phaser routine P:0x000FA8..0x001060, which the Vintage and the Smooth
 * Phaser run (Modulation block subtypes 9 and 10).
 *
 * Per stereo frame the routine sums both gained inputs to mono, advances an LFO,
 * reads the sine table at it, scales that by the depth and turns it into three
 * sweep words through the Range curves. The mono word runs through an all-pass
 * section at the middle sweep word and joins the filtered feedback; that sum runs
 * through a chain of four sections for the left output and of three for the right
 * one, whose first two sections sit at the outer sweep words, the left ones 0.01
 * lower. Each output is a weighted pair of taps of its chain, after its first
 * section and at its end: the Vintage Phaser weights the first, the Smooth Phaser
 * the second. The mono word plus the left taps is the next frame's feedback,
 * through a damping filter, a clip and a low-pass. The Mix knob's wet and dry
 * words turn each chain's output and the frame's input into the two channels.
 *
 * The step and the depth ride ramps, and the other eighteen words and the Mix
 * pair a one-pole job (gm2_job.h).
 */
#ifndef GM2_PHASER_CORE_H
#define GM2_PHASER_CORE_H

#include <stdint.h>

#include "gm2_base.h"

#define GM2_PHASER_CORE_RAMPS 2u
#define GM2_PHASER_CORE_COEFS 20u

/* The positions in coef[]: the order of the staged words X:0x098A1F..0x098A34,
 * less the middle tap weight of each chain, which no subtype sets. The first
 * GM2_PHASER_CORE_RAMPS ramp. */
enum {
  GM2_PHASER_CORE_STEP,            /* Y:0x0433: the LFO's phase step */
  GM2_PHASER_CORE_DEPTH,           /* Y:0x0434 */
  GM2_PHASER_CORE_SWEEP = 2, /* Y:0x0435..0x043A: three {slope, base} pairs of the Range curves */
  GM2_PHASER_CORE_LOW_PASS = 8, /* Y:0x0442..0x0444: the feedback's low-pass */
  GM2_PHASER_CORE_POLE = 11, /* Y:0x0449, 0x044A, 0x044D: the fixed sections' words */
  GM2_PHASER_CORE_TAP_LEFT = 14, /* Y:0x044E, 0x0450: the left chain's first and last tap */
  GM2_PHASER_CORE_TAP_RIGHT = 16, /* Y:0x0452, 0x0454: the right chain's first and last tap */
  GM2_PHASER_CORE_FEEDBACK = 18, /* Y:0x0456 */
  GM2_PHASER_CORE_RIGHT_GAIN =
      19, /* Y:0x0458: the right output's gain, negative with Phase Reverse on */
};

/* The positions in raw[]: the knobs whose turns arm a ramp. Every turn of Speed
 * or Tempo runs P:0x092EDA, which arms the step's ramp; every turn of Depth runs
 * P:0x092EAF, which arms the depth's. Each arms whatever the turn leaves the
 * staged word. */
enum {
  GM2_PHASER_CORE_RAW_SPEED,
  GM2_PHASER_CORE_RAW_DEPTH,
  GM2_PHASER_CORE_RAW_TEMPO,
  GM2_PHASER_CORE_RAWS,
};

typedef struct {
  int32_t coef[GM2_PHASER_CORE_COEFS]; /* the words the updaters stage */
  int32_t wet; /* X:0x098AC5: the Mix knob's wet word */
  int32_t dry; /* X:0x098AC6: its dry word */
  int32_t raw[GM2_PHASER_CORE_RAWS]; /* the raw values of the knobs that arm a ramp */
  unsigned note; /* the Tempo value the step follows, 0 for Speed */
} gm2_phaser_core_config;

typedef struct {
  int32_t damp[3]; /* X:0x02C2..0x02C4: the feedback's damping filter */
  int32_t low_pass; /* X:0x02C5: the feedback's low-pass */
  int32_t pair[3]; /* X:0x02C6..0x02C8: the input section */
  int32_t left[9]; /* X:0x02C9..0x02D1: the left chain, each section sharing a word with the next */
  int32_t right[7]; /* X:0x02D2..0x02D8: the right chain */
  int32_t feedback; /* X:0x02DC: the mono word plus the left taps */
  int32_t lfo_phase; /* X:0x02DD */
  int32_t coef[GM2_PHASER_CORE_COEFS]; /* the live words, gliding to the config's */
  int32_t wet; /* Y:0x045A: the live pair, Y:0x045B the dry one */
  int32_t dry;
  int32_t ramp_step[GM2_PHASER_CORE_RAMPS]; /* each ramp's step, armed by a knob turn */
  int32_t raw[GM2_PHASER_CORE_RAWS]; /* the config's raw values at the last job run */
  unsigned chunk_pos; /* frame within the chunk */
} gm2_phaser_core_state;

void gm2_phaser_core_state_init(gm2_phaser_core_state *state, const gm2_phaser_core_config *config);

void gm2_phaser_core_process_frame(gm2_phaser_core_state *state,
                                   const gm2_phaser_core_config *config,
                                   const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

void gm2_phaser_core_align(gm2_phaser_core_state *state, const gm2_phaser_core_config *config,
                           double beats);

#endif /* GM2_PHASER_CORE_H */
