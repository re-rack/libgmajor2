/* The Mix knob's two level words: the block level code P:0x08F763 turns a mix
 * percentage into the wet and dry words an effect's output stage multiplies its
 * voice sum and its input by, an equal-power pair at Out Level 0 dB.
 *
 * An effect whose block has no Mix knob runs on the words of a full mix: on the
 * Whammy the Mix callback P:0x091287 reaches P:0x0912C7, which pins its mix word
 * at 0x7FFFFF.
 */
#ifndef GM2_MIX_H
#define GM2_MIX_H

#include <stdint.h>

/* The wet and dry words of `raw` percent, 0..100. */
void gm2_mix_words(int raw, int32_t *wet, int32_t *dry);

/* P:0x08F78D: the 1.23 sine half a staged mix word selects. */
int32_t gm2_mix_curve(int32_t word);

#endif /* GM2_MIX_H */
