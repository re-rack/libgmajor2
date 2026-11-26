/* G-Major 2 Reverse Delay, Delay block subtype 3 (manual p. 48).
 *
 * It runs the shared Delay routine (gm2_delay_core.h) with `rev_step = 4` so the
 * active read tap walks backwards in time through the delay buffer (+4 words
 * minus the write pointer's 2-word step = +1 stereo frame backwards per sample).
 * Every `grain_chunks` four-frame chunks (`delay_ms` milliseconds on the device's
 * 1 kHz timer X:$FFFF8C, checked by P:0x094484), P:0x094424 zeroes the silent
 * tap's reverse offset, toggles the target tap offset between 1 and 3, and starts
 * a 501-chunk equal-power crossfade to the new reverse grain. A Delay or Tempo
 * turn runs P:0x094735, which re-anchors the timer target Y+27 to the current
 * count, so the grain period starts over.
 *
 * A tap's walk lasts two grain periods, since each splice resets the tap that is
 * silent: past 41,196 frames (858 ms) at full gain it wraps and the tap reads the
 * newest audio, a click its feedback then repeats.
 */
#ifndef GM2_REVDELAY_H
#define GM2_REVDELAY_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_delay_core.h"

typedef struct {
  int32_t coef[GM2_DELAY_CORE_COEFS]; /* X+0..19, the words the updaters stage */
  int32_t wet;                        /* X:0x098ED0: the Mix pair, X:0x098ED1 the dry one */
  int32_t dry;
  int32_t delay_ms;    /* Y+26: a grain's length in ms */
  unsigned grain_chunks; /* that length in chunks, gm2_tick_period() */
} gm2_revdelay_config;

typedef struct {
  gm2_delay_core_state core;
  int32_t delay_ms; /* the config's grain length when the grain's period last started */
  unsigned tick;    /* chunks since the last splice */
} gm2_revdelay_state;

void gm2_revdelay_state_init(gm2_revdelay_state *state, const gm2_revdelay_config *config);

void gm2_revdelay_process_split(gm2_revdelay_state *state, const gm2_revdelay_config *config,
                                const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                                int32_t *out_r, size_t frames);

void gm2_revdelay_process_f32(gm2_revdelay_state *state, const gm2_revdelay_config *config,
                              const float *in_l, const float *in_r, float *out_l, float *out_r,
                              size_t frames);

void gm2_revdelay_process_interleaved(gm2_revdelay_state *state, const gm2_revdelay_config *config,
                                      const int32_t *in, int32_t *out, size_t frames);

void gm2_revdelay_process_frame(gm2_revdelay_state *state, const gm2_revdelay_config *config,
                                const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

/* The same frame with `wrap_guard` set, which also ends the grain once the audible
 * tap's walk is within a crossfade of the buffer's end, so that the walk cannot
 * wrap while the tap has gain. The de-popped variant's own path runs this one
 * (gm2_revdelay_alt.h). */
void gm2_revdelay_advance(gm2_revdelay_state *state, const gm2_revdelay_config *config,
                          int wrap_guard, const int32_t in[GM2_CHANNELS],
                          int32_t out[GM2_CHANNELS]);

#endif /* GM2_REVDELAY_H */
