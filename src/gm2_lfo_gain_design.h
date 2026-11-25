/* The setup the Tremolo and the Panner share: the Filter block's updaters for
 * subtypes 5 and 6, called with the Tremolo/Panner UI object X:0x0988CE.
 *
 *   P:0x0918CE  Speed, Tempo   the phase step
 *   P:0x091999  Depth, Width   the depth word
 *   P:0x0918AF  (none)         the waveform smoother's pole pair
 */
#ifndef GM2_LFO_GAIN_DESIGN_H
#define GM2_LFO_GAIN_DESIGN_H

#include "gm2_lfo_gain_core.h"
#include "gm2_rate.h"

typedef struct {
  int speed;      /* 0..208: 0.050..19.95 Hz, 80 steps a decade */
  int depth;      /* 0..100 %: the Tremolo's Depth, the Panner's Width */
  int tempo;      /* 0..16: Ignore, then the note values 1 .. 1/32 triplet */
  int out_level;  /* -100..0 dB */
  double beat_ms; /* the host's beat in ms, as gm2_lfo_beat() takes it */
} gm2_lfo_gain_design_raw;

/* The step and depth words, the smoother, the block's wet word, the note and the
 * three knobs' raw values. The waveform, the lag, the pulse words, the Hi-Cut pairs
 * and the Pulse Width's raw value are the effect's to set. */
void gm2_lfo_gain_design(const gm2_rate_words *rate, const gm2_lfo_gain_design_raw *raw,
                         gm2_lfo_gain_core_config *config);

#endif /* GM2_LFO_GAIN_DESIGN_H */
