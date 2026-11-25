/* Style, Hi-Cut, Resonance and Mix to the Resonance Filter's words: the Filter
 * block's updaters for subtype 3.
 *
 * The activation P:0x091BB1 runs P:0x091F81 (Style and Res), P:0x0921BC (the
 * subtype) and then P:0x09208E with a zero frequency, which takes the Hi-Cut
 * frequency P:0x08F0DF gives and designs the twenty coefficient words
 * gm2_res_design.h describes. The words land at X:0x098928..X:0x09893B.
 *
 *   P:0x09220C  Mix  UI+61 = to_q23(Mix * 0.01), then P:0x08F763's pair
 *
 * Only Style, Hi-Cut and Res change those words.
 */
#ifndef GM2_RESFILTER_SETUP_H
#define GM2_RESFILTER_SETUP_H

#include "gm2_base.h"
#include "gm2_resfilter.h"

typedef struct {
  int style;     /* 0: 2nd order, 1: 4th order */
  int hi_cut;    /* 18..57: 160 Hz..14 kHz, 20 steps a decade */
  int resonance; /* 0..100 % */
  int mix;       /* 0..100 % */
  int out_level; /* -100..0 dB */
} gm2_resfilter_setup_raw;

int gm2_resfilter_setup_config(const gm2_context *context, const gm2_resfilter_setup_raw *raw,
                               gm2_resfilter_config *config);

#endif /* GM2_RESFILTER_SETUP_H */
