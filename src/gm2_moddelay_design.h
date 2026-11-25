/* The setup the nine chorus, flanger and vibrato effects share: the Modulation
 * block's updaters for subtypes 0..8, P:0x0938BE.
 *
 *   P:0x093884  the six voice gains
 *   P:0x093C85  Speed, Tempo: the phase step
 *   P:0x093B64  Depth, Golden Ratio: the six voice depths
 *   P:0x093CB7  Phase Reverse: the output biquad's words
 *   P:0x093D5C  Delay, Width: the six voice offsets
 *   P:0x093EA1  Feedback: the feedback word
 *   P:0x093EF9  Feedback Hi-Cut: the feedback filters' words
 *   P:0x093F60  Hi-Cut: the input Hi-Cut's words
 */
#ifndef GM2_MODDELAY_DESIGN_H
#define GM2_MODDELAY_DESIGN_H

#include "gm2_moddelay_core.h"
#include "gm2_rate.h"

/* The Modulation block's subtype numbers. */
typedef enum {
  GM2_MODDELAY_DESIGN_CCHORUS = 0,
  GM2_MODDELAY_DESIGN_ACHORUS = 1,
  GM2_MODDELAY_DESIGN_CFLANGER = 2,
  GM2_MODDELAY_DESIGN_AFLANGER = 3,
  GM2_MODDELAY_DESIGN_TRICHORUS = 4,
  GM2_MODDELAY_DESIGN_TRICHORUS_ASYM = 5,
  GM2_MODDELAY_DESIGN_TZFLANGER_NEG = 6,
  GM2_MODDELAY_DESIGN_TZFLANGER_POS = 7,
  GM2_MODDELAY_DESIGN_VIBRATO = 8,
} gm2_moddelay_design_subtype;

/* The knobs of all nine effects. An effect leaves the ones it does not have at
 * zero; the design gives those the value the device holds for them. */
typedef struct {
  int speed;         /* 0..208: 0.050..19.95 Hz, 80 steps a decade */
  int depth;         /* 0..100 % */
  int tempo;         /* 0..16: Ignore, then the note values 1 .. 1/32 triplet */
  int hi_cut;        /* 0..60: 20 Hz..20 kHz, 20 steps a decade */
  int feedback;      /* -100..100 % */
  int fb_cut;        /* 0..60: 20 Hz..20 kHz, 20 steps a decade */
  int delay;         /* 1..500: 0.1..50.0 ms */
  int width;         /* 1..100 % */
  int golden_ratio;  /* 0 Off, 1 On */
  int phase_reverse; /* 0 Off, 1 On */
  double beat_ms;    /* the host's beat in ms, as gm2_lfo_beat() takes it */
} gm2_moddelay_design_raw;

void gm2_moddelay_design(const gm2_rate_words *rate, gm2_moddelay_design_subtype subtype,
                         const gm2_moddelay_design_raw *raw, gm2_moddelay_core_config *config);

#endif /* GM2_MODDELAY_DESIGN_H */
