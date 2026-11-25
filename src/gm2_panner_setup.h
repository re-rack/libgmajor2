/* Speed, Width and Tempo to the Panner's words: the Filter block's updaters for
 * subtype 6.
 *
 *   P:0x0918CE  Speed, Tempo  the step
 *   P:0x091999  Width         the depth word
 *   P:0x0919C4  (subtype)     the sine waveform, the right channel half a cycle
 *                             behind
 *   P:0x091876  (subtype)     the Hi-Cut open
 *   P:0x0918AF  (none)        the waveform smoother
 */
#ifndef GM2_PANNER_SETUP_H
#define GM2_PANNER_SETUP_H

#include "gm2_base.h"
#include "gm2_panner.h"

typedef struct {
  int speed;      /* 0..208: 0.050..19.95 Hz, 80 steps a decade */
  int width;      /* 0..100 % */
  int tempo;      /* 0..16: Ignore, then the note values 1 .. 1/32 triplet */
  double beat_ms; /* the host's beat in ms, as gm2_lfo_beat() takes it */
} gm2_panner_setup_raw;

int gm2_panner_setup_config(const gm2_context *context, const gm2_panner_setup_raw *raw,
                            gm2_panner_config *config);

#endif /* GM2_PANNER_SETUP_H */
