/* Speed, Depth, Tempo, Feedback, Range and Phase Reverse to the Vintage Phaser's
 * words: the Modulation block's descriptor table P:0x08544D. */
#ifndef GM2_VINTPHASER_SETUP_H
#define GM2_VINTPHASER_SETUP_H

#include "gm2_base.h"
#include "gm2_vintphaser.h"

typedef struct {
  int speed;         /* 0..208: 0.050..19.95 Hz, 80 steps a decade */
  int depth;         /* 0..100 % */
  int tempo;         /* 0..16: Ignore, then the note values 1 .. 1/32 triplet */
  int feedback;      /* -100..100 % */
  int range;         /* 0 Low, 1 High */
  int phase_reverse; /* 0 Off, 1 On */
  double beat_ms;    /* the host's beat in ms, as gm2_lfo_beat() takes it */
} gm2_vintphaser_setup_raw;

int gm2_vintphaser_setup_config(const gm2_context *context, const gm2_vintphaser_setup_raw *raw,
                                gm2_vintphaser_config *config);

#endif /* GM2_VINTPHASER_SETUP_H */
