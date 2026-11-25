/* The setup the two resonance filters share: the Filter block's design routines
 * for subtypes 3 and 4.
 *
 *   P:0x091F81  the design's Q and tail multiplier, from Style and Resonance
 *   P:0x09208E  the five words of one biquad, through the bilinear transform
 *               P:0x091E7B, and P:0x091FF2, which spreads them over the two
 *               biquads of both channels
 *   P:0x091DC2  the Touch Res Filter's follower words, from Response
 *   P:0x091D78  its Sense words
 *   P:0x091CF9  its tick: the follower's step and the design it drives
 */
#ifndef GM2_RES_DESIGN_H
#define GM2_RES_DESIGN_H

#include <stdint.h>

#include "gm2_rate.h"
#include "gm2_res_core.h"
#include "gm2_swf.h"

/* A design's Q and the multiplier of its numerator. */
typedef struct {
  gm2_swf q;
  gm2_swf scale;
} gm2_res_design_shape;

/* The words the Touch Res Filter's tick reads. */
typedef struct {
  gm2_swf coeff;              /* UI+32/33: the share of the gap the follower takes */
  gm2_swf threshold;          /* UI+34/35: the gap above which it takes that share */
  gm2_swf centre;             /* UI+38/39: the design's exponent at follower level zero */
  gm2_swf span;               /* UI+40/41: the exponent per unit of follower level */
  gm2_res_design_shape shape; /* UI+42..45 */
  gm2_swf max_hz;             /* the frequency FrqMax names, the tick's ceiling */
} gm2_res_design_tick_words;

/* P:0x091F81, UI+46..49: the shape the Resonance Filter designs with. `style` is
 * 0 or 1; `resonance` is the raw value, 0..100 %. */
gm2_res_design_shape gm2_res_design_resonance(int style, int resonance);

/* P:0x091F81, UI+42..45: the shape the Touch Res Filter's tick designs with,
 * which Style alone sets. `style` is 0 or 1. */
gm2_res_design_shape gm2_res_design_touch(int style);

/* P:0x09208E and P:0x091FF2: the twenty words of a design at `hz`. Style 0 gives
 * the second biquad of each channel a fixed set, Style 1 repeats the design
 * there; the right channel takes a copy of the left one's ten words. */
void gm2_res_design_coefs(const gm2_rate_words *rate, int style, gm2_swf hz,
                          gm2_res_design_shape shape, int32_t coef[GM2_RES_CORE_COEFS]);

/* P:0x091DC2 and P:0x091D78 with the shape of gm2_res_design_touch(): the tick's
 * words from the raw Response (0..2), Sense (0..10) and FrqMax (34..54) values.
 * `style` is 0 or 1. */
void gm2_res_design_tick_words_init(gm2_res_design_tick_words *words, int style, int response,
                                    int sense, int freq_max);

/* P:0x091CF9, one tick: the follower steps towards `level`, a peak hold read with
 * gm2_tick_level(), and drives the design between 10^2.5 Hz and the ceiling.
 * `follower` is UI+36/37 and is updated; `coef` receives the twenty words. */
void gm2_res_design_tick(const gm2_rate_words *rate, const gm2_res_design_tick_words *words,
                         int style, gm2_swf *follower, int32_t level,
                         int32_t coef[GM2_RES_CORE_COEFS]);

#endif /* GM2_RES_DESIGN_H */
