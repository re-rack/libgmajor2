/* Speed 1, Speed 2, Depth, Tempo 1, Tempo 2, Attack, Release, Select, Phase
 * Reverse and Mix of the Modern Univibe to its words: the Modulation block's
 * updaters for subtype 12, descriptor table P:0x085725.
 */
#ifndef GM2_MODUNIVIBE_SETUP_H
#define GM2_MODUNIVIBE_SETUP_H

#include "gm2_base.h"
#include "gm2_modunivibe.h"

typedef struct {
  int speed_1;       /* 37..184: 0.150..10.0 Hz, 80 steps a decade; 37 stops the LFO */
  int speed_2;       /* the same */
  int depth;         /* 0..100 % */
  int tempo_1;       /* 0..16: Ignore, then the note values 1 .. 1/32 triplet */
  int tempo_2;       /* the same */
  int attack;        /* 1..100: 0.05..5.0 s */
  int release;       /* 1..100: 0.05..5.0 s */
  int select;        /* 0 Speed 1 with Tempo 1, 1 Speed 2 with Tempo 2 */
  int phase_reverse; /* 0 Off, 1 On */
  int mix;           /* 0..100 % */
  double beat_ms;    /* the host's beat in ms, as gm2_lfo_beat() takes it */
} gm2_modunivibe_setup_raw;

int gm2_modunivibe_setup_config(const gm2_context *context, const gm2_modunivibe_setup_raw *raw,
                                gm2_modunivibe_config *config);

#endif /* GM2_MODUNIVIBE_SETUP_H */
