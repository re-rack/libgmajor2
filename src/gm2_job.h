/* The firmware's parameter jobs: the records its task scheduler steps between
 * audio chunks, so that a knob turn glides instead of jumping. The ports step
 * every job of an effect once after each chunk of GM2_CHUNK_FRAMES frames.
 *
 * Three kinds exist. A one-pole job moves a live word a share of the way to its
 * staged word. A ramp moves a live word by a fixed step that the parameter's
 * updater arms on every turn of its knob. A tap crossfade moves a delay line's
 * audible read from one tap to the other when the staged offset changes.
 *
 * Every word here is a signed 24-bit value.
 */
#ifndef GM2_JOB_H
#define GM2_JOB_H

#include <stdint.h>

#include "gm2_dsp_math.h"

/* The step word of every one-pole record but the Pitch block's ratio pair. */
#define GM2_JOB_SMOOTH_STEP 0x008371

/* The step a tap crossfade's position moves per run, 501 runs from rail to rail. */
#define GM2_JOB_FADE_STEP 0x008312

/* P:0x0001FD, and P:0x00026F for a group of words: one run of a one-pole job. The
 * live word moves by `step` times the gap to the staged word and lands on it once
 * that move is within the record's snap word, which is 1 in every record. */
static inline int32_t gm2_job_smooth(int32_t live, int32_t staged, int32_t step) {
  if (live == staged) {
    return staged;
  }
  int64_t b = gm2_word_acc(live);
  int32_t half = gm2_store24(gm2_asr_n(gm2_acc56(gm2_word_acc(staged) - b), 1));
  int64_t a = gm2_mpy(half, step);
  a = gm2_mag56(a) > gm2_mag56(gm2_word_acc(1)) ? gm2_addl(b, a) : gm2_word_acc(staged);
  return gm2_store24(gm2_round24(a));
}

/* P:0x09605F, which a parameter's updater runs on every turn of its knob: the
 * step a ramp takes for a move from `live` to `target`, the gap times X:0x0993B4
 * and at least 1. Every caller caps the step at 0x200000, which no gap between
 * two 24-bit words reaches. */
int32_t gm2_job_ramp_arm(int32_t live, int32_t target);

/* P:0x0001B9: one run of a ramp. The live word moves by `step` towards the staged
 * word and lands on it once the gap is within `step`. A record starts with a step
 * of 1. */
static inline int32_t gm2_job_ramp(int32_t live, int32_t staged, int32_t step) {
  if (live == staged) {
    return staged;
  }
  int64_t b = gm2_word_acc(live);
  int64_t a = gm2_sub24(gm2_word_acc(staged), live);
  if (a < 0) {
    b = gm2_sub24(b, step);
  } else if (a > 0) {
    b = gm2_add24(b, step);
  }
  if (gm2_mag56(a) <= gm2_mag56(gm2_word_acc(step))) {
    b = gm2_word_acc(staged);
  }
  return gm2_store24(b);
}

/* A tap crossfade record. Tap 1 is audible at rest. */
typedef struct {
  int32_t step; /* 0 while settled, +-GM2_JOB_FADE_STEP while fading */
  int32_t pos;  /* 0x7FFFFF with tap 0 audible, -0x800000 with tap 1 */
} gm2_job_fade;

/* The tap whose gain a settled record holds at zero. */
static inline unsigned gm2_job_fade_silent(const gm2_job_fade *fade) {
  return fade->pos < 0 ? 0u : 1u;
}

/* The record at rest and its two tap gains. */
void gm2_job_fade_init(gm2_job_fade *fade, int32_t tap_gain[2]);

/* P:0x00033C and P:0x000301, the settled check: while the fade moves it does
 * nothing; once it has stopped the silent tap takes `offset`, and the fade starts
 * towards that tap if the two taps' offsets then differ. Returns 1 while a fade
 * runs and 0 when both taps sit on `offset`. */
int gm2_job_fade_check(gm2_job_fade *fade, int32_t tap_offset[2], int32_t offset);

/* P:0x0002B7 behind P:0x00033A and P:0x000293: one run of the equal-power
 * crossfade, which moves the position by its step and writes the two tap gains,
 * a parabola and its complement. */
void gm2_job_fade_step(gm2_job_fade *fade, int32_t tap_gain[2]);

#endif /* GM2_JOB_H */
