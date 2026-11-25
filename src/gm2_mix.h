/* The two knobs every effect block's level code P:0x08F763 reads: the Mix
 * percentage and the Out Level in dB. Its three staged words are the input word,
 * the wet word an output stage multiplies its voice sum by and the dry word it
 * multiplies the input by: each of the last two is the Out Level's gain times the
 * equal-power sine half a staged mix word selects.
 *
 * An effect whose block has no Mix knob runs on the words of a full mix: on the
 * Whammy the Mix callback P:0x091287 reaches P:0x0912C7, which pins its mix word
 * at 0x7FFFFF.
 */
#ifndef GM2_MIX_H
#define GM2_MIX_H

#include <stdint.h>

/* The wet and dry words of `out_level` dB, -100..0, at `mix` percent, 0..100. */
void gm2_mix_words(int out_level, int mix, int32_t *wet, int32_t *dry);

/* P:0x08F78D: the 1.23 sine half a staged mix word selects. */
int32_t gm2_mix_curve(int32_t word);

#endif /* GM2_MIX_H */
