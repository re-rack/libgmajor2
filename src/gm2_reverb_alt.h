/* The de-popped Reverb: gm2_reverb.h with a crossfade of the words its kernel
 * reads straight, a group glide (gm2_job.h) over its eighty coefficient words, and
 * the hand-back to the Reverb's own path once settled (gm2_handback.h).
 *
 * The kernel reads the early reflections' offsets and weights and the pre-delay
 * tap's distance straight from the config every frame, so a turn of Shape, Size,
 * Pre-Delay or Hi Color steps the taps at once: the early signal jumps to
 * unrelated words of the buffer, and with it the tank's input, and the high band
 * bursts by up to 29 dB. Here a turn crossfades to the new set over
 * GM2_REVERB_ALT_FADE_FRAMES: both sets' early reflections run, each through its
 * own comb and biquad state, the incoming ones starting as a copy of the
 * outgoing ones; the pre-delay tap is read at both distances; and the two blend
 * along the device's own crossfade curve, which is eased at both ends. One tank
 * carries both, so the tail never splits. A turn while a fade runs waits for it
 * to end and then fades on to the newest set, so fast turns chain fades and stop
 * on the last one.
 */
#ifndef GM2_REVERB_ALT_H
#define GM2_REVERB_ALT_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_base.h"
#include "gm2_handback.h"
#include "gm2_job.h"
#include "gm2_reverb.h"

#define GM2_REVERB_ALT_FADE_FRAMES 2048u

typedef gm2_reverb_config gm2_reverb_alt_config;

/* The words the kernel reads straight, which a knob turn rewrites: the walk's
 * offsets, the pre-delay tap's among them, and the early reflections' weights. */
typedef struct {
  int32_t offsets[GM2_REVERB_OFFSETS];
  int32_t early[GM2_CHANNELS][GM2_REVERB_EARLY_TAPS];
} gm2_reverb_alt_words;

/* The incoming early path's state, which lives while a fade runs. */
typedef struct {
  int32_t comb[GM2_CHANNELS][GM2_REVERB_EARLY_COMBS];
  gm2_reverb_biquad biquad[GM2_CHANNELS][GM2_REVERB_EARLY_BIQUADS];
} gm2_reverb_alt_early;

typedef struct {
  gm2_reverb_state exact;    /* the Reverb, always run */
  gm2_reverb_state own;      /* the de-popped copy, run from a turn until the hand-back */
  gm2_reverb_alt_words play; /* the straight words own plays */
  gm2_reverb_alt_words next; /* the ones a fade runs towards */
  gm2_reverb_alt_early incoming;
  int32_t from[GM2_REVERB_COEFS];
  int32_t to[GM2_REVERB_COEFS];
  gm2_job_group group;
  unsigned fade; /* frames into the fade, 0 with none running */
  gm2_handback handback;
} gm2_reverb_alt_state;

void gm2_reverb_alt_state_init(gm2_reverb_alt_state *state, const gm2_reverb_alt_config *config);

void gm2_reverb_alt_process_split(gm2_reverb_alt_state *state, const gm2_reverb_alt_config *config,
                                  const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                                  int32_t *out_r, size_t frames);

void gm2_reverb_alt_process_f32(gm2_reverb_alt_state *state, const gm2_reverb_alt_config *config,
                                const float *in_l, const float *in_r, float *out_l, float *out_r,
                                size_t frames);

void gm2_reverb_alt_process_interleaved(gm2_reverb_alt_state *state,
                                        const gm2_reverb_alt_config *config, const int32_t *in,
                                        int32_t *out, size_t frames);

void gm2_reverb_alt_process_frame(gm2_reverb_alt_state *state, const gm2_reverb_alt_config *config,
                                  const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_REVERB_ALT_H */
