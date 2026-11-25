/* The setup the two phasers share: the Modulation block's updaters for subtypes
 * 9 and 10, P:0x092B92.
 *
 *   P:0x092D6D  Range: the three sweep curves and the fixed sections' words
 *   P:0x092D1C  the feedback's low-pass
 *   P:0x092E88  Feedback
 *   P:0x092EAF  Depth
 *   P:0x092EDA  Speed, Tempo: the phase step
 *   P:0x092E54  the tap weights, by subtype
 *   P:0x092F2B  Phase Reverse: the right output's gain
 *   P:0x08F763  Mix: the wet and dry words the output stage multiplies by
 */
#ifndef GM2_PHASER_DESIGN_H
#define GM2_PHASER_DESIGN_H

#include "gm2_phaser_core.h"
#include "gm2_rate.h"

/* The Modulation block's subtype numbers. */
typedef enum {
  GM2_PHASER_DESIGN_VINTAGE = 9,
  GM2_PHASER_DESIGN_SMOOTH = 10,
} gm2_phaser_design_subtype;

typedef struct {
  int speed;         /* 0..208: 0.050..19.95 Hz, 80 steps a decade */
  int depth;         /* 0..100 % */
  int tempo;         /* 0..16: Ignore, then the note values 1 .. 1/32 triplet */
  int feedback;      /* -100..100 % */
  int range;         /* 0 Low, 1 High */
  int phase_reverse; /* 0 Off, 1 On */
  int mix;           /* 0..100 % wet */
  double beat_ms;    /* the host's beat in ms, as gm2_lfo_beat() takes it */
} gm2_phaser_design_raw;

void gm2_phaser_design(const gm2_rate_words *rate, gm2_phaser_design_subtype subtype,
                       const gm2_phaser_design_raw *raw, gm2_phaser_core_config *config);

#endif /* GM2_PHASER_DESIGN_H */
