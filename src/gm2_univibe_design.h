/* The setup the two Univibes share: the Modulation block's updaters for subtypes
 * 11 and 12, P:0x09311B.
 *
 *   P:0x0932C2  the four stages' curves
 *   P:0x0933CD  Attack, Release: the speed slew's poles; and the lamp's poles
 *   P:0x09339D  Depth
 *   P:0x093565  Phase Reverse: the stages' weight in the left output
 *   P:0x09349C  Speed 1, Speed 2, Tempo 1, Tempo 2, Select: the LFO speed's target
 */
#ifndef GM2_UNIVIBE_DESIGN_H
#define GM2_UNIVIBE_DESIGN_H

#include "gm2_rate.h"
#include "gm2_univibe_core.h"

/* The Modulation block's subtype numbers. */
typedef enum {
  GM2_UNIVIBE_DESIGN_VINTAGE = 11,
  GM2_UNIVIBE_DESIGN_MODERN = 12,
} gm2_univibe_design_subtype;

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
  double beat_ms;    /* the host's beat in ms, as gm2_lfo_beat() takes it */
} gm2_univibe_design_raw;

void gm2_univibe_design(const gm2_rate_words *rate, gm2_univibe_design_subtype subtype,
                        const gm2_univibe_design_raw *raw, gm2_univibe_core_config *config);

#endif /* GM2_UNIVIBE_DESIGN_H */
