/* Speed, Depth, Tempo, Width, Phase Reverse, Mix and Out Level to the
 * Tri-Chorus Asymmetric's words: the Modulation block's updaters for subtype 5
 * (gm2_moddelay_design.h), descriptor table P:0x085BDD.
 */
#ifndef GM2_TRICHORUS_ASYM_SETUP_H
#define GM2_TRICHORUS_ASYM_SETUP_H

#include "gm2_base.h"
#include "gm2_trichorus_asym.h"

typedef struct {
  int speed;         /* 0..208: 0.050..19.95 Hz, 80 steps a decade */
  int depth;         /* 0..100 % */
  int tempo;         /* 0..16: Ignore, then the note values 1 .. 1/32 triplet */
  int width;         /* 1..100 % */
  int phase_reverse; /* 0 Off, 1 On */
  int mix;           /* 0..100 % wet */
  int out_level;     /* -100..0 dB */
  double beat_ms;    /* the host's beat in ms, as gm2_lfo_beat() takes it */
} gm2_trichorus_asym_setup_raw;

int gm2_trichorus_asym_setup_config(const gm2_context *context,
                                    const gm2_trichorus_asym_setup_raw *raw,
                                    gm2_trichorus_asym_config *config);

#endif /* GM2_TRICHORUS_ASYM_SETUP_H */
