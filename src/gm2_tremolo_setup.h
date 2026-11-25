/* Speed, Depth, Tempo, Type, Pulse Width and Hi-Cut to the Tremolo's words: the
 * Filter block's updaters for subtype 5.
 *
 *   P:0x0918CE  Speed, Tempo, Pulse Width  the step, the pulse word and Soft's
 *                                          two slopes
 *   P:0x091999  Depth                      the depth word
 *   P:0x0919C4  Type                       the waveform, with no lag
 *   P:0x091876  Hi-Cut                     the Hi-Cut's pole pair, both channels
 *   P:0x0918AF  (none)                     the waveform smoother
 */
#ifndef GM2_TREMOLO_SETUP_H
#define GM2_TREMOLO_SETUP_H

#include "gm2_base.h"
#include "gm2_tremolo.h"

typedef struct {
  int speed;       /* 0..208: 0.050..19.95 Hz, 80 steps a decade */
  int depth;       /* 0..100 % */
  int tempo;       /* 0..16: Ignore, then the note values 1 .. 1/32 triplet */
  int type;        /* 0 Soft, 1 Hard */
  int pulse_width; /* 0..100 % */
  int hi_cut;      /* 0..60: 20 Hz..20 kHz, 20 steps a decade */
  double beat_ms;  /* the host's beat in ms, as gm2_lfo_beat() takes it */
  int out_level;   /* -100..0 dB */
} gm2_tremolo_setup_raw;

int gm2_tremolo_setup_config(const gm2_context *context, const gm2_tremolo_setup_raw *raw,
                             gm2_tremolo_config *config);

#endif /* GM2_TREMOLO_SETUP_H */
