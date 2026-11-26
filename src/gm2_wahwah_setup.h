/* Sweep, Range, Mix and Out Level to the Wah Wah's words: the Filter block's
 * updaters for subtype 0.
 *
 *   P:0x09250B  Sweep      Y:0x40E = to_q23(Sweep / 100), the cubic's input
 *   P:0x092527  Range      Y:0x40A = 2 for Low, 1 for High
 *   P:0x09277A  Mix        UI+67 = to_q23(Mix * 0.01), then P:0x08F763's pair
 *   P:0x08F763  Out Level  its gain on both words of that pair
 */
#ifndef GM2_WAHWAH_SETUP_H
#define GM2_WAHWAH_SETUP_H

#include "gm2_base.h"
#include "gm2_wahwah.h"

typedef struct {
  int sweep;     /* 0..100 % */
  int range;     /* 0 Low, 1 High */
  int mix;       /* 0..100 % wet */
  int out_level; /* -100..0 dB */
} gm2_wahwah_setup_raw;

int gm2_wahwah_setup_config(const gm2_context *context, const gm2_wahwah_setup_raw *raw,
                            gm2_wahwah_config *config);

#endif /* GM2_WAHWAH_SETUP_H */
