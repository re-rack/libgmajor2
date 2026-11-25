/* Speed, Depth, Tempo, Hi-Cut, Feedback and Feedback Hi-Cut to the Classic Flanger's words: the
 * Modulation block's updaters for subtype 2, descriptor table P:0x0859FD.
 */
#ifndef GM2_CFLANGER_SETUP_H
#define GM2_CFLANGER_SETUP_H

#include "gm2_base.h"
#include "gm2_cflanger.h"

typedef struct {
  int speed; /* 0..208: 0.050..19.95 Hz, 80 steps a decade */
  int depth; /* 0..100 % */
  int tempo; /* 0..16: Ignore, then the note values 1 .. 1/32 triplet */
  int hi_cut; /* 0..60: 20 Hz..20 kHz, 20 steps a decade */
  int feedback; /* -100..100 % */
  int fb_cut; /* 0..60: 20 Hz..20 kHz, 20 steps a decade */
  int mix; /* 0..100 % */
  int out_level; /* -100..0 dB */
  double beat_ms; /* the host's beat in ms, as gm2_lfo_beat() takes it */
} gm2_cflanger_setup_raw;

int gm2_cflanger_setup_config(const gm2_context *context, const gm2_cflanger_setup_raw *raw,
                              gm2_cflanger_config *config);

#endif /* GM2_CFLANGER_SETUP_H */
