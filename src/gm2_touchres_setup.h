/* Style, Sense, Response, FrqMax and Mix to the Touch Res Filter's words: the Filter
 * block's updaters for subtype 4.
 *
 * The activation P:0x091BB1 runs P:0x091F81 (Style, and the Res value at index 12
 * that subtype 4 does not read), P:0x0921BC, and then once the tick P:0x091CF9
 * with the level word X:0x0B03 still zero. The tick designs the twenty
 * coefficient words gm2_res_design.h describes; the level its follower sees on the
 * device is the Noise Gate's peak hold, which is zero before the first frame, so
 * the settled set is a tick at level 0.
 *
 *   P:0x09220C  Mix  UI+61 = to_q23(Mix * 0.01), then P:0x08F763's pair
 *
 * Only Style, Sense, Response and FrqMax reach those words.
 */
#ifndef GM2_TOUCHRES_SETUP_H
#define GM2_TOUCHRES_SETUP_H

#include "gm2_base.h"
#include "gm2_touchres.h"

typedef struct {
  int style;    /* 0: 2nd order, 1: 4th order */
  int sense;    /* 0..10 */
  int response; /* 0 Slow, 1 Medium, 2 Fast */
  int freq_max; /* 34..54: 1 kHz..10 kHz, 20 steps a decade */
  int mix;      /* 0..100 % */
  int out_level; /* -100..0 dB */
} gm2_touchres_setup_raw;

int gm2_touchres_setup_config(const gm2_context *context, const gm2_touchres_setup_raw *raw,
                              gm2_touchres_config *config);

#endif /* GM2_TOUCHRES_SETUP_H */
