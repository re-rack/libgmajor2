/* Voice, Pan, Delay and Level of both voices, Key, Scale and Mix to the
 * Intelligent Pitch Shifter's words: the Pitch block's updaters for subtype 4
 * (gm2_pitch_design.h), descriptor table P:0x084A81.
 */
#ifndef GM2_IPITCH_SETUP_H
#define GM2_IPITCH_SETUP_H

#include "gm2_base.h"
#include "gm2_ipitch.h"

typedef struct {
  int voice_1; /* -12..12 scale steps */
  int voice_2; /* -12..12 scale steps */
  int pan_1;   /* -50..50: left..right */
  int pan_2;   /* -50..50: left..right */
  int delay_1; /* 0..350 ms */
  int delay_2; /* 0..350 ms */
  int key;     /* 0..11: C..B */
  int scale;   /* 0..17: Ionian .. Phrygian Major, the rows of gm2_pitch_scale_table */
  int level_1; /* -100..0 dB */
  int level_2; /* -100..0 dB */
  int mix;     /* 0..100 % wet */
} gm2_ipitch_setup_raw;

int gm2_ipitch_setup_config(const gm2_context *context, const gm2_ipitch_setup_raw *raw,
                            gm2_ipitch_config *config);

#endif /* GM2_IPITCH_SETUP_H */
