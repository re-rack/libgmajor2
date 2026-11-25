/* Speed, Tempo, Shape, Range and Mix to the Auto Wah's words: the Filter block's
 * updaters for subtype 2.
 *
 *   P:0x092477  Shape  Y:0x414 = Shape
 *   P:0x0924C1  Speed, Tempo  the LFO's staged phase step per chunk
 *   P:0x092527  Range  Y:0x40A = 2 for Low, 1 for High
 *   P:0x09277A  Mix    UI+67 = to_q23(Mix * 0.01), then P:0x08F763's pair
 *
 * The Tempo branch stages the tempo rate's step and writes the nearest Speed step
 * back into the Speed parameter; the staged step the smoothing job follows is the
 * tempo rate's.
 */
#ifndef GM2_AUTOWAH_SETUP_H
#define GM2_AUTOWAH_SETUP_H

#include "gm2_autowah.h"
#include "gm2_base.h"

typedef struct {
  int speed;      /* 0..208: 0.050..19.95 Hz, 80 steps a decade */
  int tempo;      /* 0..16: Ignore, then the note values 1 .. 1/32 triplet */
  int shape;      /* 0 Triangle, 1 Sine */
  int range;      /* 0 Low, 1 High */
  int mix;        /* 0..100 % */
  double beat_ms; /* the host's beat in ms, as gm2_lfo_beat() takes it */
} gm2_autowah_setup_raw;

int gm2_autowah_setup_config(const gm2_context *context, const gm2_autowah_setup_raw *raw,
                             gm2_autowah_config *config);

#endif /* GM2_AUTOWAH_SETUP_H */
