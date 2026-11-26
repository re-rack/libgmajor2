/* Delay 1, Delay 2, Tempo 1, Tempo 2, Feedback 1, Feedback 2, Feedback Hi-Cut,
 * Feedback Lo-Cut, Pan 1, Pan 2, Mix and Out Level to the Dual Delay's words:
 * the Delay block's updaters for subtype 2 (gm2_delay_design.h).
 */
#ifndef GM2_DUALDELAY_SETUP_H
#define GM2_DUALDELAY_SETUP_H

#include "gm2_base.h"
#include "gm2_dualdelay.h"

typedef struct {
  int delay_1;    /* 0..1800 ms */
  int delay_2;    /* 0..1800 ms */
  int tempo_1;    /* 0..16: Ignore, then the note values 1 .. 1/32 triplet */
  int tempo_2;    /* the same */
  int feedback_1; /* 0..100 % */
  int feedback_2; /* 0..100 % */
  int fb_hi_cut;  /* 40..60: 2 kHz..20 kHz, 20 steps a decade */
  int fb_lo_cut;  /* 0..40: 20 Hz..2 kHz, 20 steps a decade */
  int pan_1;      /* -50..50: left..right */
  int pan_2;      /* -50..50: left..right */
  int mix;        /* 0..100 % wet */
  int out_level;  /* -100..0 dB */
  double beat_ms; /* the host's beat in ms, as gm2_lfo_beat() takes it */
} gm2_dualdelay_setup_raw;

int gm2_dualdelay_setup_config(const gm2_context *context, const gm2_dualdelay_setup_raw *raw,
                               gm2_dualdelay_config *config);

#endif /* GM2_DUALDELAY_SETUP_H */
