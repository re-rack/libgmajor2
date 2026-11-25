/* The Tremolo and Panner audio routine, P:0x000E5A..0x000EE8, which both effects
 * run (Filter block subtypes 5 and 6), each with its own setup words.
 *
 * Per stereo frame an LFO phase advances by a step and wraps, and the right
 * channel reads it a fixed lag behind. A waveform routine turns each channel's
 * phase into a level between 0 and 1: a triangle peaking at the pulse word
 * (Soft), a square that is high below it (Hard) or the interpolated sine table
 * (Sine), each smoothed by a one-pole. A channel's gain is 1 - depth * level; the
 * input times that gain runs through a one-pole Hi-Cut and the block's wet word.
 * The routine reads no input-gain word and its dry term is a zero word.
 *
 * The pulse, step, slope and depth words ride ramps, the two Hi-Cut pairs and the
 * block's wet word a one-pole job (gm2_job.h). The waveform, the lag and the
 * smoother are written straight to the words the routine reads.
 */
#ifndef GM2_LFO_GAIN_CORE_H
#define GM2_LFO_GAIN_CORE_H

#include <stdint.h>

#include "gm2_base.h"

#define GM2_LFO_GAIN_CORE_RAMPS 5u
#define GM2_LFO_GAIN_CORE_COEFS (GM2_LFO_GAIN_CORE_RAMPS + 2u * GM2_CHANNELS)

/* The positions in coef[]. The first GM2_LFO_GAIN_CORE_RAMPS ramp. */
enum {
  GM2_LFO_GAIN_CORE_PULSE, /* Y:0x3B3, where Soft peaks and Hard falls */
  GM2_LFO_GAIN_CORE_STEP,  /* Y:0x3B4, the phase step per frame */
  GM2_LFO_GAIN_CORE_RISE,  /* Y:0x3B7, Soft's slope below the pulse word */
  GM2_LFO_GAIN_CORE_FALL,  /* Y:0x3B8, Soft's slope above it */
  GM2_LFO_GAIN_CORE_DEPTH, /* Y:0x3B9 */
  /* Y:0x3BC/0x3BD left and Y:0x3BF/0x3C0 right: each Hi-Cut's pole, 1 - pole */
  GM2_LFO_GAIN_CORE_HI_CUT,
};

/* The positions in raw[]: the knobs whose turns arm a ramp. Every turn of Speed,
 * Tempo or Pulse Width runs P:0x0918CE, which arms the pulse, step, rise and fall
 * ramps; every turn of Depth or Width runs P:0x091999, which arms the depth ramp.
 * Each arms whatever the turn leaves the staged words. */
enum {
  GM2_LFO_GAIN_CORE_RAW_SPEED,
  GM2_LFO_GAIN_CORE_RAW_TEMPO,
  GM2_LFO_GAIN_CORE_RAW_PULSE_WIDTH,
  GM2_LFO_GAIN_CORE_RAW_DEPTH,
  GM2_LFO_GAIN_CORE_RAWS,
};

/* The waveform routine Y:0x3B5 points at. */
typedef enum {
  GM2_LFO_GAIN_CORE_WAVE_SOFT, /* P:0x000E8C */
  GM2_LFO_GAIN_CORE_WAVE_HARD, /* P:0x000EAF */
  GM2_LFO_GAIN_CORE_WAVE_SINE, /* P:0x000EC1, gm2_lfo_sine_table */
} gm2_lfo_gain_core_wave;

/* The sine wave indexes its table with the phase's top eight bits, so the step
 * and the lag have to keep both channels' phases in 0..0x7FFFFF, as the setups'
 * own values do. */
typedef struct {
  int32_t coef[GM2_LFO_GAIN_CORE_COEFS]; /* the words the updaters stage */
  gm2_lfo_gain_core_wave wave;           /* Y:0x3B5 */
  int32_t lag;                           /* Y:0x3B6, how far the right channel's phase lags */
  int32_t smooth[2]; /* Y:0x3BA/0x3BB, the waveform smoother: pole, 1 - pole */
  int32_t wet;       /* Y:0x3C1: the block's wet word, the level code's */
  int32_t raw[GM2_LFO_GAIN_CORE_RAWS]; /* the raw values of the knobs that arm a ramp */
  unsigned note; /* the Tempo value the step follows, 0 for Speed */
} gm2_lfo_gain_core_config;

typedef struct {
  int32_t phase; /* X:0x2AB, the LFO's phase */
  int32_t level[GM2_CHANNELS]; /* X:0x2AE/0x2AF, the smoothed waveform levels */
  int32_t wet; /* Y:0x3C1: the live wet word, gliding to the config's */
  int32_t hi_cut[GM2_CHANNELS]; /* X:0x2B0/0x2B1, the Hi-Cut's states */
  int32_t coef[GM2_LFO_GAIN_CORE_COEFS]; /* the live words, gliding to the config's */
  int32_t ramp_step[GM2_LFO_GAIN_CORE_RAMPS]; /* each ramp's step, armed by a knob turn */
  int32_t raw[GM2_LFO_GAIN_CORE_RAWS]; /* the config's raw values at the last job run */
  unsigned chunk_pos; /* frame within the chunk */
} gm2_lfo_gain_core_state;

void gm2_lfo_gain_core_state_init(gm2_lfo_gain_core_state *state,
                                  const gm2_lfo_gain_core_config *config);

void gm2_lfo_gain_core_process_frame(gm2_lfo_gain_core_state *state,
                                     const gm2_lfo_gain_core_config *config,
                                     const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

void gm2_lfo_gain_core_align(gm2_lfo_gain_core_state *state, const gm2_lfo_gain_core_config *config,
                             double beats);

#endif /* GM2_LFO_GAIN_CORE_H */
