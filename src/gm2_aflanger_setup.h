/* Speed, Depth, Tempo, Hi-Cut, Feedback, Feedback Hi-Cut, Delay, Golden Ratio and Phase Reverse to
 * the Advanced Flanger's words: the Modulation block's updaters for subtype 3, descriptor table
 * P:0x085AED.
 */
#ifndef GM2_AFLANGER_SETUP_H
#define GM2_AFLANGER_SETUP_H

#include "gm2_aflanger.h"
#include "gm2_base.h"

typedef struct {
  int speed; /* 0..208: 0.050..19.95 Hz, 80 steps a decade */
  int depth; /* 0..100 % */
  int tempo; /* 0..16: Ignore, then the note values 1 .. 1/32 triplet */
  int hi_cut; /* 0..60: 20 Hz..20 kHz, 20 steps a decade */
  int feedback; /* -100..100 % */
  int fb_cut; /* 0..60: 20 Hz..20 kHz, 20 steps a decade */
  int delay; /* 1..500: 0.1..50.0 ms */
  int golden_ratio; /* 0 Off, 1 On */
  int phase_reverse; /* 0 Off, 1 On */
  int mix; /* 0..100 % */
  int out_level; /* -100..0 dB */
  double beat_ms; /* the host's beat in ms, as gm2_lfo_beat() takes it */
} gm2_aflanger_setup_raw;

int gm2_aflanger_setup_config(const gm2_context *context, const gm2_aflanger_setup_raw *raw,
                              gm2_aflanger_config *config);

#endif /* GM2_AFLANGER_SETUP_H */
