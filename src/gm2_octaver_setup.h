/* Direction, Range, Mix and Out Level to the Octaver's words: the Pitch block's
 * updaters for subtype 2 (gm2_pitch_design.h), descriptor table P:0x0848A1.
 */
#ifndef GM2_OCTAVER_SETUP_H
#define GM2_OCTAVER_SETUP_H

#include "gm2_base.h"
#include "gm2_octaver.h"

typedef struct {
  int direction; /* 0 Down, 1 Up */
  int range;     /* 1..2 octaves */
  int mix;       /* 0..100 % wet */
  int out_level; /* -100..0 dB */
} gm2_octaver_setup_raw;

int gm2_octaver_setup_config(const gm2_context *context, const gm2_octaver_setup_raw *raw,
                             gm2_octaver_config *config);

#endif /* GM2_OCTAVER_SETUP_H */
