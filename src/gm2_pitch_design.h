/* The setup the five Pitch effects share: the Pitch block's updaters for subtypes
 * 0..4, P:0x090937.
 *
 *   P:0x090A72  the detector's period window and the crossfade's speed
 *   P:0x090B50  the tuning reference
 *   P:0x091017  Scale: the Intelligent Pitch Shifter's harmony grid
 *   P:0x0910BD  Voice, Key: its two curves
 *   P:0x090C89  Voice, or Pitch, Direction and Range: the two pitch ratios
 *   P:0x090D7F  Delay: the two voices' delays
 *   P:0x090E51  Feedback, Pan, Level: the feedbacks and the pan gains
 *
 * The window and the reference depend on the device's Tuner Ref and Tuner Range,
 * X:0x099A61 and X:0x099A63, which are globals, not Pitch parameters: the setup
 * holds their factory 440 Hz and Guitar.
 */
#ifndef GM2_PITCH_DESIGN_H
#define GM2_PITCH_DESIGN_H

#include "gm2_pitch_core.h"
#include "gm2_rate.h"

/* The Pitch block's subtype numbers. */
typedef enum {
  GM2_PITCH_DESIGN_DETUNE = 0,
  GM2_PITCH_DESIGN_WHAMMY = 1,
  GM2_PITCH_DESIGN_OCTAVER = 2,
  GM2_PITCH_DESIGN_PITCH = 3,
  GM2_PITCH_DESIGN_IPITCH = 4,
} gm2_pitch_design_subtype;

/* The knobs of all five effects. An effect leaves the ones it does not have at
 * zero. */
typedef struct {
  /* -100..100 cents on the Detune, -2400..2400 cents on the Pitch Shifter,
   * -12..12 scale steps on the Intelligent Pitch Shifter */
  int voice[GM2_PITCH_CORE_VOICES];
  int pan[GM2_PITCH_CORE_VOICES];      /* -50..50: left..right */
  int delay[GM2_PITCH_CORE_VOICES];    /* 0..50 ms on the Detune, 0..350 ms otherwise */
  int feedback[GM2_PITCH_CORE_VOICES]; /* 0..100 %, 100 % together at most */
  int level[GM2_PITCH_CORE_VOICES];    /* -100..0 dB */
  int key;                             /* 0..11: C..B */
  int scale;                           /* 0..17: the rows of gm2_pitch_scale_table */
  int pitch;                           /* 0..100 % of the Whammy's range */
  int direction;                       /* 0 Down, 1 Up */
  int range;                           /* 1..2 octaves */
} gm2_pitch_design_raw;

void gm2_pitch_design(const gm2_rate_words *rate, gm2_pitch_design_subtype subtype,
                      const gm2_pitch_design_raw *raw, gm2_pitch_core_config *config);

#endif /* GM2_PITCH_DESIGN_H */
