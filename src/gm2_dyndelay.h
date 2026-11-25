/* G-Major 2 Dynamic Delay, Delay block subtype 1 (manual p. 48).
 *
 * It runs the shared Delay routine (gm2_delay_core.h) with the first line at the
 * main delay time (feeding back into itself and into the second line) and the
 * second line at the stereo Offset time. Every gm2_tick_chunks() four-frame
 * chunks (13 ms), it reads the Noise Gate's level detector (gm2_gate_detector.h)
 * on its own input and runs the ducking envelope P:0x0944BE: while the input
 * level exceeds Sense, the delay output gain walks towards Damp at the 20 ms
 * attack rate; at or below Sense, it walks back towards full scale at the
 * Release rate.
 */
#ifndef GM2_DYNDELAY_H
#define GM2_DYNDELAY_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_delay_core.h"
#include "gm2_gate_detector.h"

#define GM2_DYNDELAY_DUCK_WORDS 4u

/* The positions in duck[]. */
enum {
  GM2_DYNDELAY_DUCK_SENSE,   /* X+26: the level above which the delays duck */
  GM2_DYNDELAY_DUCK_DAMP,    /* X+27: the gain they duck to */
  GM2_DYNDELAY_DUCK_ATTACK,  /* X+28: the share of the gap a tick takes on the way down */
  GM2_DYNDELAY_DUCK_RELEASE, /* X+29: and on the way back */
};

typedef struct {
  /* X+0..19, the words the updaters stage. The output matrix's first pair is the
   * ducking tick's and stays zero here. */
  int32_t coef[GM2_DELAY_CORE_COEFS];
  int32_t tap_offset[GM2_DELAY_CORE_LINES]; /* X:0x098E2A the Delay's, X:0x098E35 the Offset's */
  int32_t duck[GM2_DYNDELAY_DUCK_WORDS];    /* X+26..29 */
  unsigned tick_chunks;                     /* chunks per tick, gm2_tick_chunks() */
} gm2_dyndelay_config;

typedef struct {
  gm2_delay_core_state core;
  gm2_gate_detector detector; /* the level detector on the effect's own input */
  int32_t duck;               /* X+30: the ducking envelope */
  int32_t duck_gain;          /* X+8 and X+9 as the last tick staged them */
  unsigned tick;              /* chunks since the last tick */
} gm2_dyndelay_state;

void gm2_dyndelay_state_init(gm2_dyndelay_state *state, const gm2_dyndelay_config *config);

void gm2_dyndelay_process_split(gm2_dyndelay_state *state, const gm2_dyndelay_config *config,
                                const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                                int32_t *out_r, size_t frames);

void gm2_dyndelay_process_f32(gm2_dyndelay_state *state, const gm2_dyndelay_config *config,
                              const float *in_l, const float *in_r, float *out_l, float *out_r,
                              size_t frames);

void gm2_dyndelay_process_interleaved(gm2_dyndelay_state *state, const gm2_dyndelay_config *config,
                                      const int32_t *in, int32_t *out, size_t frames);

void gm2_dyndelay_process_frame(gm2_dyndelay_state *state, const gm2_dyndelay_config *config,
                                const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_DYNDELAY_H */
