/* Freq, Gain and Width to the EQ's words: the Gate & EQ block's updater
 * P:0x0906B6, which runs the per-band chain P:0x095C3C and the word conversion
 * P:0x090620.
 *
 *   Freq   10^(1 + (raw + 24)/80) Hz; 241 is Off
 *   Gain   10^(raw/20); 0 dB leaves the band flat
 *   Width  2^((raw - 10)/3) octaves
 *
 * The chain takes another branch for a frequency of half the sample rate or
 * more, which is not ported: the setup returns -1 for such a band.
 */
#ifndef GM2_EQ_SETUP_H
#define GM2_EQ_SETUP_H

#include "gm2_base.h"
#include "gm2_eq.h"

typedef struct {
  struct {
    int freq;  /* 25..240: 41 Hz..20 kHz, 80 steps a decade; 241 is Off */
    int gain;  /* -12..12 dB */
    int width; /* 3..16: 0.2..4 octaves, three steps an octave */
  } band[GM2_EQ_BANDS];
} gm2_eq_setup_raw;

int gm2_eq_setup_config(const gm2_context *context, const gm2_eq_setup_raw *raw,
                        gm2_eq_config *config);

#endif /* GM2_EQ_SETUP_H */
