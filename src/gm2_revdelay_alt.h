/* The de-popped Reverse Delay: gm2_revdelay.h whose grain also ends before the
 * audible tap's walk reaches the buffer's end, handed back to the delay's own path
 * (gm2_handback.h).
 *
 * A Delay or Tempo turn re-anchors the grain's period (P:0x094735), so a hand that
 * keeps turning holds every splice off, and a tap's walk runs past the 43,200
 * frames the buffer holds. With the tap still at gain the read jumps to the newest
 * audio: a click its feedback repeats. Here a turn starts a copy of the delay whose
 * grain also ends once the audible tap has walked 40,000 frames, which leaves room
 * for the 2,004-frame crossfade before the buffer's end. The copy splices on its
 * first frame, so it never runs the delay's grain phase, and two seconds of it play
 * before the fade back; a grain of the longest Delay setting is one second, and
 * every further turn moves the fade out again.
 *
 * The state holds the delay twice, 1.4 MB, nearly all of it the two buffers; the
 * first frame of a turn copies one.
 *
 * What it cannot remove: the de-popped grain is spliced where the delay's is not,
 * so the two paths' taps differ until the hand-back, and the delay's own buffer
 * keeps the clicks it made meanwhile. With Feedback above zero the hand-back plays
 * those back out of the delay, echo by echo, for as long as the feedback sustains
 * them; at zero it carries none of them. The click the delay makes on its own once
 * its grain period reaches 858 ms (one tap's walk then wraps under gain every
 * second grain) is the device's behaviour: the alt keeps it, and from the hand-back
 * on it is all the alt plays.
 */
#ifndef GM2_REVDELAY_ALT_H
#define GM2_REVDELAY_ALT_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_base.h"
#include "gm2_handback.h"
#include "gm2_revdelay.h"

typedef gm2_revdelay_config gm2_revdelay_alt_config;

typedef struct {
  gm2_revdelay_state exact; /* the delay, always run */
  gm2_revdelay_state own;   /* the de-popped copy, run from a turn until the hand-back */
  gm2_handback handback;
} gm2_revdelay_alt_state;

void gm2_revdelay_alt_state_init(gm2_revdelay_alt_state *state,
                                 const gm2_revdelay_alt_config *config);

void gm2_revdelay_alt_process_split(gm2_revdelay_alt_state *state,
                                    const gm2_revdelay_alt_config *config, const int32_t *in_l,
                                    const int32_t *in_r, int32_t *out_l, int32_t *out_r,
                                    size_t frames);

void gm2_revdelay_alt_process_f32(gm2_revdelay_alt_state *state,
                                  const gm2_revdelay_alt_config *config, const float *in_l,
                                  const float *in_r, float *out_l, float *out_r, size_t frames);

void gm2_revdelay_alt_process_interleaved(gm2_revdelay_alt_state *state,
                                          const gm2_revdelay_alt_config *config, const int32_t *in,
                                          int32_t *out, size_t frames);

void gm2_revdelay_alt_process_frame(gm2_revdelay_alt_state *state,
                                    const gm2_revdelay_alt_config *config,
                                    const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_REVDELAY_ALT_H */
