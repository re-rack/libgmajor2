/* Sense, Response and Range to the Touch Wah's words: the Filter block's
 * updaters for subtype 1.
 *
 *   P:0x0925F3  Response  UI+46/47 the follower coefficient K, UI+48/49 its
 *                         threshold T
 *   P:0x0925A9  Sense     UI+52/53 the follower level that reads as zero,
 *                         UI+54/55 the sweep word per unit
 *   P:0x092678  (none)    Y:0x415 the envelope's decay pole
 *   P:0x092527  Range     Y:0x40A = 2 for Low, 1 for High
 *   P:0x09277A  Mix       UI+67 = to_q23(Mix * 0.01), then P:0x08F763's pair
 *
 * gm2_tick.h owns the Response and Sense derivations; the tick's sweep word is
 * to_q23(clamp(centre + span * follow)).
 */
#ifndef GM2_TOUCHWAH_SETUP_H
#define GM2_TOUCHWAH_SETUP_H

#include "gm2_base.h"
#include "gm2_touchwah.h"

typedef struct {
  int sense;    /* 0..10 */
  int response; /* 0 Slow, 1 Medium, 2 Fast */
  int range;    /* 0 Low, 1 High */
  int mix;      /* 0..100 % */
} gm2_touchwah_setup_raw;

int gm2_touchwah_setup_config(const gm2_context *context, const gm2_touchwah_setup_raw *raw,
                              gm2_touchwah_config *config);

#endif /* GM2_TOUCHWAH_SETUP_H */
