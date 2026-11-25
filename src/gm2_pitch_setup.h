/* Voice, Pan, Delay, Feedback and Level of both voices, and Mix, to the Pitch
 * Shifter's words: the Pitch block's updaters for subtype 3 (gm2_pitch_design.h),
 * descriptor table P:0x084991.
 */
#ifndef GM2_PITCH_SETUP_H
#define GM2_PITCH_SETUP_H

#include "gm2_base.h"
#include "gm2_pitch.h"

typedef struct {
  int voice_1;    /* -2400..2400 cents */
  int voice_2;    /* -2400..2400 cents */
  int pan_1;      /* -50..50: left..right */
  int pan_2;      /* -50..50: left..right */
  int delay_1;    /* 0..350 ms */
  int delay_2;    /* 0..350 ms */
  int feedback_1; /* 0..100 %, 100 % together with the other at most */
  int feedback_2; /* 0..100 % */
  int level_1;    /* -100..0 dB */
  int level_2;    /* -100..0 dB */
  int mix;        /* 0..100 % wet */
} gm2_pitch_setup_raw;

int gm2_pitch_setup_config(const gm2_context *context, const gm2_pitch_setup_raw *raw,
                           gm2_pitch_config *config);

#endif /* GM2_PITCH_SETUP_H */
