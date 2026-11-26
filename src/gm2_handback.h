/* The hand-back the de-popped variants (gm2_X_alt) share. Such a variant runs the
 * firmware's path at all times, so that its state is the effect's own, and from a
 * knob turn on also a de-popped path that starts as a copy of it. The variant
 * plays the de-popped path until the two have settled and then fades back to the
 * firmware's, from where its output is the effect's, sample for sample.
 *
 * Settled is the variant's call: both paths' words on the config's. The fade
 * starts once the two outputs have agreed to within GM2_HANDBACK_CALM_LSB for
 * GM2_HANDBACK_CALM_FRAMES, or after GM2_HANDBACK_WAIT_FRAMES settled whatever
 * they do, which bounds the wait for a path that will not converge. A new turn
 * while it fades turns the fade around.
 */
#ifndef GM2_HANDBACK_H
#define GM2_HANDBACK_H

#include <stdint.h>

#include "gm2_base.h"

#define GM2_HANDBACK_CALM_LSB 16
#define GM2_HANDBACK_CALM_FRAMES 2400u
#define GM2_HANDBACK_WAIT_FRAMES 96000u
#define GM2_HANDBACK_FADE_FRAMES 1024u

typedef struct {
  unsigned own;     /* 1 while the de-popped path runs */
  unsigned fading;  /* 1 while the fade moves towards the firmware's path */
  uint32_t fade;    /* frames into the fade, 0..GM2_HANDBACK_FADE_FRAMES */
  uint32_t calm;    /* frames the two outputs have agreed for */
  uint32_t settled; /* frames both paths have been settled for */
} gm2_handback;

/* The firmware's path alone. */
void gm2_handback_init(gm2_handback *handback);

/* A turn: while the firmware's path plays alone, the de-popped path, a copy of the
 * firmware's made at this frame, plays from here; while it runs, the wait starts
 * over and a fade under way turns around. */
void gm2_handback_start(gm2_handback *handback);

/* One frame while the de-popped path runs: the output out of the two paths'.
 * Returns 0 once the fade is over and the firmware's path plays alone again. */
int gm2_handback_frame(gm2_handback *handback, int settled, const int32_t exact[GM2_CHANNELS],
                       const int32_t own[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_HANDBACK_H */
