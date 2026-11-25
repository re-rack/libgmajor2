/* The Univibe routine P:0x00107F..0x001116, which the Vintage and the Modern
 * Univibe run (Modulation block subtypes 11 and 12).
 *
 * Per stereo frame the routine sums both gained inputs to mono, clips the sum
 * softly and runs it through four all-pass stages whose coefficients step towards
 * targets the lamp sets. Each output is that word mixed with the channel's own
 * gained input, through a second soft clip and the block's wet word.
 *
 * On the first frame of each chunk the LFO's speed slews towards its target, on
 * the Attack words while it has to rise and on the Release words otherwise, and
 * the phase advances by it. The sine at that phase, scaled by the depth around
 * one half, runs through the waveshaper into a brightness; the lamp follows the
 * brightness, faster up than down; and the lamp's word places the four stages'
 * targets on their curves.
 *
 * The depth rides a ramp and the other twenty words one-pole jobs
 * (gm2_job.h). The LFO speed's target and the waveshaper's two words are written
 * straight to the words the routine reads.
 */
#ifndef GM2_UNIVIBE_CORE_H
#define GM2_UNIVIBE_CORE_H

#include <stdint.h>

#include "gm2_base.h"

#define GM2_UNIVIBE_CORE_STAGES 4u
#define GM2_UNIVIBE_CORE_RAMPS 1u
#define GM2_UNIVIBE_CORE_COEFS 21u

/* The positions in coef[]. The first GM2_UNIVIBE_CORE_RAMPS ramp, and raw[] holds
 * their knobs' values at the same positions: P:0x09339D arms the depth's ramp on
 * every turn of Depth, whatever the turn leaves the staged word. */
enum {
  GM2_UNIVIBE_CORE_DEPTH, /* Y:0x0471 */
  /* Y:0x046D..0x0470: the speed slew's {new, keep} pair for Release, then for
   * Attack */
  GM2_UNIVIBE_CORE_SPEED_POLES = 1,
  /* Y:0x0477..0x047A: the lamp's {new, keep} pair while it falls, then while it
   * rises */
  GM2_UNIVIBE_CORE_LAMP_POLES = 5,
  GM2_UNIVIBE_CORE_CURVES = 9, /* Y:0x047B..0x0482: each stage's {slope, base} */
  /* Y:0x0487: the stages' weight in the left output, negative with Phase Reverse
   * on */
  GM2_UNIVIBE_CORE_WET = 17,
  /* Y:0x0488..Y:0x048A: the left input's weight, then the right channel's two */
  GM2_UNIVIBE_CORE_MIX = 18,
};

typedef struct {
  int32_t coef[GM2_UNIVIBE_CORE_COEFS]; /* the words the updaters stage */
  int32_t lfo_speed;                    /* Y:0x046C: the LFO speed's target */
  int32_t shaper[2]; /* Y:0x0474/0x0475: the waveshaper's linear and square weights */
  int32_t raw[GM2_UNIVIBE_CORE_RAMPS]; /* the raw value of the knob whose turns arm the ramp */
  unsigned note; /* the Tempo value the speed follows, 0 for Speed */
} gm2_univibe_core_config;

typedef struct {
  int32_t lfo_speed; /* X:0x02E0: the slewed LFO speed */
  int32_t lfo_phase; /* X:0x02E1 */
  int32_t lamp; /* X:0x02E2: the lamp's brightness */
  /* X:0x02E3..0x02E7: each stage's last input, then the last stage's output */
  int32_t stage[GM2_UNIVIBE_CORE_STAGES + 1u];
  int32_t pole[GM2_UNIVIBE_CORE_STAGES]; /* X:0x02E8, 0x02EA, 0x02EC, 0x02EE: each stage's
                                            coefficient */
  int32_t
      pole_step[GM2_UNIVIBE_CORE_STAGES]; /* X:0x02E9, 0x02EB, 0x02ED, 0x02EF: its step per frame */
  int32_t coef[GM2_UNIVIBE_CORE_COEFS]; /* the live words, gliding to the config's */
  int32_t ramp_step[GM2_UNIVIBE_CORE_RAMPS]; /* the ramp's step, armed by a turn of its knob */
  int32_t raw[GM2_UNIVIBE_CORE_RAMPS]; /* the config's raw value at the last job run */
  unsigned chunk_pos; /* frame within the chunk */
} gm2_univibe_core_state;

void gm2_univibe_core_state_init(gm2_univibe_core_state *state,
                                 const gm2_univibe_core_config *config);

void gm2_univibe_core_process_frame(gm2_univibe_core_state *state,
                                    const gm2_univibe_core_config *config,
                                    const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

void gm2_univibe_core_align(gm2_univibe_core_state *state, const gm2_univibe_core_config *config,
                            double beats);

#endif /* GM2_UNIVIBE_CORE_H */
