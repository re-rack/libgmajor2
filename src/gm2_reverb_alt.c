#include "gm2_reverb_alt.h"

#include "gm2_dsp_math.h"
#include "gm2_process.h"
#include "gm2_reverb_kernel.h"

#include <string.h>

/* The fade's frame count as a shift, so that its curve runs in whole words. */
enum { REVERB_ALT_FADE_SHIFT = 11 };

_Static_assert(GM2_REVERB_ALT_FADE_FRAMES == 1u << REVERB_ALT_FADE_SHIFT, "the fade's shift");

/* Whether the words own plays differ from the config's straight ones. */
static int reverb_alt_words_moved(const gm2_reverb_alt_state *state,
                                  const gm2_reverb_alt_config *config) {
  return memcmp(state->play.offsets, config->offsets, sizeof state->play.offsets) != 0 ||
         memcmp(state->play.early, config->early, sizeof state->play.early) != 0;
}

static void reverb_alt_words_set(gm2_reverb_alt_words *words, const gm2_reverb_alt_config *config) {
  memcpy(words->offsets, config->offsets, sizeof words->offsets);
  memcpy(words->early, config->early, sizeof words->early);
}

/* The share of the way the incoming set has come, in 2^-23 units, on the device's
 * own crossfade curve (gm2_job_fade_step): a parabola eased at both ends, so the
 * blend starts and stops flat instead of stepping its slope. 0 at the fade's first
 * frame, a whole one on its last. */
static int64_t reverb_alt_share(unsigned fade) {
  const int64_t p = 2 * (int64_t)fade - (int64_t)GM2_REVERB_ALT_FADE_FRAMES;
  const int64_t q = p * (1 << (23 - REVERB_ALT_FADE_SHIFT));
  return (1 << 22) + q - gm2_asr_n(q * (q < 0 ? -q : q), 24);
}

/* The blend of two of the kernel's words, `share` of the way from the first to the
 * second. */
static int32_t reverb_alt_blend(int32_t from, int32_t to, int64_t share) {
  return from + (int32_t)gm2_asr_n(((int64_t)to - (int64_t)from) * share + (1 << 22), 23);
}

/* The chunk's job run: the eighty words glide as one group, and the Mix pair
 * through the one-pole job the Reverb itself runs on it. */
static void reverb_alt_jobs(gm2_reverb_alt_state *state, const gm2_reverb_alt_config *config) {
  (void)gm2_job_group_run(&state->group, state->own.coef, state->from, state->to, config->coef,
                          GM2_REVERB_COEFS);
  if (state->own.wet != config->wet || state->own.dry != config->dry) {
    state->own.wet = gm2_job_smooth(state->own.wet, config->wet, GM2_JOB_SMOOTH_STEP);
    state->own.dry = gm2_job_smooth(state->own.dry, config->dry, GM2_JOB_SMOOTH_STEP);
  }
}

/* The Reverb's frame, P:0x0012D2..0x0013DE, on own's words: the same kernel with
 * the straight words crossfading from play to next, then the chunk's job run. */
static void reverb_alt_own_frame(gm2_reverb_alt_state *state, const gm2_reverb_alt_config *config,
                                 const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  gm2_reverb_state *own = &state->own;
  const int64_t share = state->fade != 0u ? reverb_alt_share(state->fade) : 0;
  const int32_t *c = own->coef;
  const int32_t *o = state->play.offsets;
  const uint32_t origin = own->write;

  const int32_t in_l = gm2_s24((uint32_t)in[0]);
  const int32_t in_r = gm2_s24((uint32_t)in[1]);
  int64_t b = 2 * (int64_t)GM2_LEVEL_INPUT * in_l;
  b += 2 * (int64_t)in_r * GM2_LEVEL_INPUT;
  own->buffer[origin] = gm2_store24(gm2_asr_n(b, 2));

  gm2_reverb_kernel_tank(own, o, config->tank_signs, origin);

  gm2_reverb_kernel_walk walk = gm2_reverb_kernel_walk_start(o, origin);
  gm2_reverb_kernel_walk next = gm2_reverb_kernel_walk_start(state->next.offsets, origin);
  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    int32_t early = gm2_reverb_kernel_early(own, &walk, state->play.early[channel],
                                            own->comb[channel], own->biquad[channel], channel);
    if (state->fade != 0u) {
      const int32_t coming = gm2_reverb_kernel_early(own, &next, state->next.early[channel],
                                                     state->incoming.comb[channel],
                                                     state->incoming.biquad[channel], channel);
      early = reverb_alt_blend(early, coming, share);
    }
    own->early_out[channel] = early;
  }

  int32_t pre_delay = own->buffer[gm2_reverb_kernel_step(origin, o[GM2_REVERB_KERNEL_PRE_DELAY])];
  if (state->fade != 0u) {
    const int32_t coming = own->buffer[gm2_reverb_kernel_step(
        origin, state->next.offsets[GM2_REVERB_KERNEL_PRE_DELAY])];
    pre_delay = reverb_alt_blend(pre_delay, coming, share);
  }
  own->write = (origin + 1u) & (GM2_REVERB_BUFFER_WORDS - 1u);

  int64_t right = 2 * (int64_t)c[GM2_REVERB_ROOM] * own->early_out[1];
  int64_t left = 2 * (int64_t)c[GM2_REVERB_ROOM + 1] * own->early_out[0];
  right += 2 * (int64_t)c[GM2_REVERB_LEVEL] * own->tank_out[1];
  left += 2 * (int64_t)c[GM2_REVERB_LEVEL + 1] * own->tank_out[0];
  own->pre_delay = gm2_store24(2 * (int64_t)c[GM2_REVERB_PRE_DELAY] * pre_delay);
  const int32_t wet = own->wet;
  const int32_t dry = own->dry;
  const int64_t wet_l = gm2_asl(2 * (int64_t)wet * gm2_store24(left));
  const int64_t wet_r = gm2_asl(2 * (int64_t)wet * gm2_store24(right));
  out[0] = gm2_store24(gm2_round24(wet_l + 2 * (int64_t)in_l * dry));
  out[1] = gm2_store24(gm2_round24(wet_r + 2 * (int64_t)in_r * dry));

  /* The fade's frame count; on its last frame the incoming set plays alone, so
   * that its own state carries the identical arithmetic on from there. */
  if (state->fade != 0u) {
    if (state->fade < GM2_REVERB_ALT_FADE_FRAMES) {
      state->fade++;
    } else {
      state->play = state->next;
      memcpy(own->comb, state->incoming.comb, sizeof state->incoming.comb);
      memcpy(own->biquad, state->incoming.biquad, sizeof state->incoming.biquad);
      state->fade = 0;
    }
  }

  if (++own->chunk_pos < GM2_CHUNK_FRAMES) {
    return;
  }
  own->chunk_pos = 0;
  reverb_alt_jobs(state, config);
}

/* One stereo frame. A turn shows on the frame where the kernel would first read the
 * new straight words, so the copy starts from words that all still belong to the old
 * design. */
static void reverb_alt_frame(gm2_reverb_alt_state *state, const gm2_reverb_alt_config *config,
                             const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  if (state->fade == 0u && reverb_alt_words_moved(state, config)) {
    if (!state->handback.own) {
      state->own = state->exact;
      memcpy(state->to, state->exact.coef, sizeof state->to);
      gm2_handback_start(&state->handback);
    }
    reverb_alt_words_set(&state->next, config);
    memcpy(state->incoming.comb, state->own.comb, sizeof state->incoming.comb);
    memcpy(state->incoming.biquad, state->own.biquad, sizeof state->incoming.biquad);
    state->fade = 1u;
  }

  int32_t exact[GM2_CHANNELS];
  gm2_reverb_process_frame(&state->exact, config, in, exact);
  if (!state->handback.own) {
    out[0] = exact[0];
    out[1] = exact[1];
    return;
  }

  int32_t own[GM2_CHANNELS];
  reverb_alt_own_frame(state, config, in, own);

  const int settled = state->fade == 0u && !reverb_alt_words_moved(state, config) &&
                      memcmp(state->own.coef, config->coef, sizeof config->coef) == 0 &&
                      state->own.wet == config->wet && state->own.dry == config->dry &&
                      memcmp(state->exact.coef, config->coef, sizeof config->coef) == 0 &&
                      state->exact.wet == config->wet && state->exact.dry == config->dry;
  (void)gm2_handback_frame(&state->handback, settled, exact, own, out);
}

void gm2_reverb_alt_state_init(gm2_reverb_alt_state *state, const gm2_reverb_alt_config *config) {
  gm2_reverb_state_init(&state->exact, config);
  state->own = state->exact;
  reverb_alt_words_set(&state->play, config);
  state->next = state->play;
  memcpy(state->from, config->coef, sizeof state->from);
  memcpy(state->to, config->coef, sizeof state->to);
  state->group.remain = 0;
  state->fade = 0;
  gm2_handback_init(&state->handback);
}

GM2_PROCESS_DEFINE(reverb_alt, reverb_alt_frame)
