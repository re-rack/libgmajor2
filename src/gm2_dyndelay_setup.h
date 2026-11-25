/* Dynamic Delay parameter setup, Delay block subtype 1: Delay, Tempo, Feedback,
 * Feedback Hi-Cut, Feedback Lo-Cut, Offset, Sense, Damp and Release.
 */
#ifndef GM2_DYNDELAY_SETUP_H
#define GM2_DYNDELAY_SETUP_H

#include "gm2_base.h"
#include "gm2_dyndelay.h"

typedef struct {
  int delay;      /* 0..1800 ms */
  int tempo;      /* 0..16: Ignore, then the note values 1 .. 1/32 triplet */
  int feedback;   /* 0..100 % */
  int fb_hi_cut;  /* 40..60: 2 kHz..20 kHz, 20 steps a decade */
  int fb_lo_cut;  /* 0..40: 20 Hz..2 kHz, 20 steps a decade */
  int offset;     /* 0..200 ms: how much later the second line plays */
  int sense;      /* -50..0 dB */
  int damp;       /* 0..100 dB */
  int release;    /* 11..21: 20..1000 ms */
  double beat_ms; /* the host's beat in ms, as gm2_lfo_beat() takes it */
} gm2_dyndelay_setup_raw;

int gm2_dyndelay_setup_config(const gm2_context *context, const gm2_dyndelay_setup_raw *raw,
                              gm2_dyndelay_config *config);

#endif /* GM2_DYNDELAY_SETUP_H */
