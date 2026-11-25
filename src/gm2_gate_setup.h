/* Mode, Threshold, Damping and Release to the Noise Gate's words: the Gate & EQ
 * block's updaters P:0x0904E8, P:0x090555, P:0x090513 and P:0x090582.
 *
 *   Mode       the knee's slope, 0.1875 or 0.625
 *   Threshold  log2(gain)/64 per dB
 *   Damping    the same, negated: the floor the gain falls to
 *   Release    the slew step, a blend and two one-pole coefficients
 *
 * The larger mode word drives the knee further down; the manual's Off/Hard/Soft
 * names cannot be matched to the two raw values. The device shows threshold -61
 * as Off, but the updater converts it like any other value. Only Release depends
 * on the sample rate.
 */
#ifndef GM2_GATE_SETUP_H
#define GM2_GATE_SETUP_H

#include "gm2_base.h"
#include "gm2_gate.h"

typedef struct {
  int mode;      /* 0..1 */
  int threshold; /* -61..0 dB; -61 is shown as Off */
  int damping;   /* 0..90 dB */
  int release;   /* 3..200 dB/s */
} gm2_gate_setup_raw;

int gm2_gate_setup_config(const gm2_context *context, const gm2_gate_setup_raw *raw,
                          gm2_gate_config *config);

#endif /* GM2_GATE_SETUP_H */
