/* Threshold, Ratio, Attack, Release and Gain to the Compressor's words: the
 * block's updaters P:0x094D5C and P:0x094E18.
 *
 *   Threshold  log2(gain)/64 per dB, around the kernel's own headroom
 *   Ratio      1/R - 1, with R out of gm2_ratio_table
 *   Gain       the same scale, on top of the headroom the kernel shifts back
 *              and of the gain a threshold below -20 dB makes up for
 *   Attack     the detector's pole and the gain's pole pair while it falls
 *   Release    the recovery step and the gain's pole pair while it recovers
 *
 * Attack and Release index gm2_time_table. Only P:0x094E18 depends on the sample
 * rate.
 */
#ifndef GM2_COMP_SETUP_H
#define GM2_COMP_SETUP_H

#include "gm2_base.h"
#include "gm2_comp.h"

typedef struct {
  int threshold; /* -40..0 dB */
  int ratio;     /* 0..15: Off, 1.12:1 .. 64:1, Infinite */
  int attack;    /* 3..14: 1..70 ms */
  int release;   /* 13..23: 50..2000 ms */
  int gain;      /* -6..6 dB */
} gm2_comp_setup_raw;

int gm2_comp_setup_config(const gm2_context *context, const gm2_comp_setup_raw *raw,
                          gm2_comp_config *config);

#endif /* GM2_COMP_SETUP_H */
