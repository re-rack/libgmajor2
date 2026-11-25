/* The filter of the wah routine P:0x000F24..0x000FA3, which Wah Wah, Touch Wah
 * and Auto Wah run (Filter block subtypes 0..2).
 *
 * Per stereo frame the routine multiplies both input words by the block's input
 * gain and runs each channel through a double-precision state-variable filter at
 * the live frequency word and a soft clip, then blends that result with the
 * frame's input sample through the Mix knob's wet and dry words. On the first
 * frame of each chunk a cubic turns the sweep word into the frequency's target,
 * and one-pole jobs (gm2_job.h) move the live frequency and the two level words
 * towards theirs.
 *
 * The routine also follows the input's envelope and runs an LFO. Those belong to
 * the effects that read them, the Touch Wah and the Auto Wah.
 */
#ifndef GM2_WAH_CORE_H
#define GM2_WAH_CORE_H

#include <stdint.h>

#include "gm2_base.h"

typedef struct {
  int64_t svf[GM2_CHANNELS][2]; /* L:0x00E..0x011, two 48-bit states a channel */
  int32_t sweep;                /* Y:0x40E: the word the cubic reads, which the effect sets */
  int32_t freq_target;          /* X:0x09899E: the cubic of the sweep word */
  int32_t freq;                 /* Y:0x402: the live frequency word */
  int32_t wet;                  /* Y:0x41A: the live level words, Y:0x41B the dry one */
  int32_t dry;
  unsigned chunk_pos; /* frame within the chunk */
} gm2_wah_core_state;

/* Silent filters at `sweep`, the live frequency on its cubic and the level words on
 * `wet` and `dry`. */
void gm2_wah_core_state_init(gm2_wah_core_state *state, int32_t sweep, int32_t wet, int32_t dry);

/* One stereo frame with the frequency shift `shift`, Y:0x40A, and the Mix knob's
 * `wet` and `dry` words, then the chunk's job run once its last frame is done.
 * Returns 1 when the frame ended a chunk and 0 otherwise, for an effect that acts
 * between chunks. */
int gm2_wah_core_process_frame(gm2_wah_core_state *state, int32_t shift, int32_t wet, int32_t dry,
                               const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_WAH_CORE_H */
