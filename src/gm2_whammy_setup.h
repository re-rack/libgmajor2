/* Pitch, Direction, Range and Out Level to the Whammy's words: the Pitch
 * block's updaters for subtype 1 (gm2_pitch_design.h), descriptor table
 * P:0x0847B1.
 */
#ifndef GM2_WHAMMY_SETUP_H
#define GM2_WHAMMY_SETUP_H

#include "gm2_base.h"
#include "gm2_whammy.h"

typedef struct {
  int pitch;     /* 0..100 % of the range */
  int direction; /* 0 Down, 1 Up */
  int range;     /* 1..2 octaves */
  int out_level; /* -100..0 dB */
} gm2_whammy_setup_raw;

int gm2_whammy_setup_config(const gm2_context *context, const gm2_whammy_setup_raw *raw,
                            gm2_whammy_config *config);

#endif /* GM2_WHAMMY_SETUP_H */
