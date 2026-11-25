/* Voice L, Voice R, Delay L and Delay R to the Detune's words: the Pitch
 * block's updaters for subtype 0 (gm2_pitch_design.h), descriptor table
 * P:0x0846C1.
 */
#ifndef GM2_DETUNE_SETUP_H
#define GM2_DETUNE_SETUP_H

#include "gm2_base.h"
#include "gm2_detune.h"

typedef struct {
  int voice_l; /* -100..100 cents */
  int voice_r; /* -100..100 cents */
  int delay_l; /* 0..50 ms */
  int delay_r; /* 0..50 ms */
} gm2_detune_setup_raw;

int gm2_detune_setup_config(const gm2_context *context, const gm2_detune_setup_raw *raw,
                            gm2_detune_config *config);

#endif /* GM2_DETUNE_SETUP_H */
