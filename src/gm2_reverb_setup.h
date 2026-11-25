/* Decay, Pre-Delay, Shape, Size, Hi Color, Hi Factor, Lo Color, Lo Factor, Room
 * Level, Reverb Level, Diffuse and Mix to the Reverb's words: the Reverb block's
 * updaters, one setup chain for all four subtypes.
 *
 *   P:0x09520C  Room Level, Reverb Level, Shape, Size  the Room and Reverb levels
 *   P:0x0952D4  Decay, Hi Color, Hi Factor, Lo Color, Lo Factor  the tank's
 *               damping bands
 *   P:0x0954A4  Decay, Hi Color, Hi Factor, Lo Color, Lo Factor, Diffuse  the two
 *               diffusers
 *   P:0x095601  sample rate                            the pointers' offsets
 *   P:0x095730  Shape, Size, Pre-Delay, sample rate    the early reflections'
 *                                                      offsets
 *   P:0x09526A  Hi Color, Hi Factor, sample rate       the input one-pole
 *   P:0x095973  Shape, Size, Hi Color, sample rate     the early reflections'
 *                                                      weights and comb feedbacks
 *   P:0x095A71  Shape, Hi Color, sample rate           the early reflections'
 *                                                      biquads
 *   P:0x095710  (none)                                 the tank's signs
 *
 * Mix is not one of them: the level code P:0x08F763 stages the input, wet and
 * dry words at X:0x099046..0x099048 from the level struct at UI+63, at Out Level
 * 0 dB.
 *
 * Spring, Hall, Room and Plate are four factory settings of this one setup.
 */
#ifndef GM2_REVERB_SETUP_H
#define GM2_REVERB_SETUP_H

#include "gm2_base.h"
#include "gm2_reverb.h"

typedef struct {
  int decay;        /* 1..200: 0.1..20.0 s */
  int pre_delay;    /* 0..100 ms */
  int shape;        /* 0..2: Round, Curved, Square */
  int size;         /* 0..7: Box, Tiny, Small, Medium, Large, XL, Grand, Huge */
  int hi_color;     /* 0..6: Wool, Warm, Real, Clear, Bright, Crisp, Glass */
  int hi_factor;    /* -25..25 */
  int lo_color;     /* 0..6: Thick, Round, Real, Light, Tight, Thin, NoBass */
  int lo_factor;    /* -25..25 */
  int room_level;   /* -100..0 dB */
  int reverb_level; /* -100..0 dB */
  int diffuse;      /* -25..25 */
  int mix;          /* 0..100 % wet */
} gm2_reverb_setup_raw;

int gm2_reverb_setup_config(const gm2_context *context, const gm2_reverb_setup_raw *raw,
                            gm2_reverb_config *config);

#endif /* GM2_REVERB_SETUP_H */
