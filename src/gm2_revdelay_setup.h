/* Reverse Delay parameter setup, Delay block subtype 3: Delay, Tempo, Feedback,
 * Feedback Hi-Cut, Feedback Lo-Cut and Mix.
 */
#ifndef GM2_REVDELAY_SETUP_H
#define GM2_REVDELAY_SETUP_H

#include "gm2_base.h"
#include "gm2_revdelay.h"

typedef struct {
  int delay;      /* 50..1000 ms */
  int tempo;      /* 0..16: Ignore, then the note values 1 .. 1/32 triplet */
  int feedback;   /* 0..100 % */
  int fb_hi_cut;  /* 40..60: 2 kHz..20 kHz, 20 steps a decade */
  int fb_lo_cut;  /* 0..40: 20 Hz..2 kHz, 20 steps a decade */
  int mix;        /* 0..100 % wet */
  double beat_ms; /* the host's beat in ms, as gm2_lfo_beat() takes it */
} gm2_revdelay_setup_raw;

int gm2_revdelay_setup_config(const gm2_context *context, const gm2_revdelay_setup_raw *raw,
                              gm2_revdelay_config *config);

#endif /* GM2_REVDELAY_SETUP_H */
