/* G-Major 2 Noise Gate, in the Gate & EQ block (algorithm 6): the kernel
 * P:0x000C47..0x000CBF.
 *
 * Per stereo frame the kernel multiplies both input words by the block's input
 * gain, runs each channel through a one-pole whose two coefficients the gain
 * computer rewrites, applies the block's wet word, and follows max(|L|, |R|) with
 * an instant-attack, one-pole-decay envelope (gm2_gate_detector.h).
 *
 * On the first frame of each chunk it also runs the gain computer: log2 of the
 * envelope, the knee below the threshold, a target slewed down by one release
 * step per chunk and clamped to the damping floor, and a smoothed gain word that
 * exp2 turns back into the two coefficients the next frames use.
 *
 * Mode, Threshold and Damping ride ramps and the four Release words a one-pole
 * job (gm2_job.h).
 */
#ifndef GM2_GATE_H
#define GM2_GATE_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_base.h"

#define GM2_GATE_RAMPS 3u
#define GM2_GATE_RELEASE_COEFS 4u
#define GM2_GATE_COEFS (GM2_GATE_RAMPS + GM2_GATE_RELEASE_COEFS)

/* The positions in coef[]. The first GM2_GATE_RAMPS ramp, and raw[] holds their
 * knobs' values at the same positions. */
enum {
  GM2_GATE_MODE,      /* Y:0x379, staged at X:0x09874A by P:0x0904E8 */
  GM2_GATE_THRESHOLD, /* Y:0x376, staged at X:0x098749 by P:0x090555 */
  GM2_GATE_DAMPING,   /* Y:0x37A, staged at X:0x09874B by P:0x090513 */
  /* Y:0x37B..0x37E, staged at X:0x098745..0x098748 by P:0x090582: the slew step,
   * the blend and the two one-pole coefficients */
  GM2_GATE_RELEASE,
};

typedef struct {
  int32_t coef[GM2_GATE_COEFS]; /* the words the updaters stage */
  int32_t raw[GM2_GATE_RAMPS];  /* the raw values of the knobs whose turns arm a ramp */
} gm2_gate_config;

typedef struct {
  int32_t filter[GM2_CHANNELS];      /* X:0x2A0/0x2A1, the one-pole's state */
  int32_t envelope;                  /* X:0x2A2 */
  int32_t gain;                      /* X:0x2A3, the log2 gain word */
  int32_t target;                    /* X:0x2A4, the slewed target */
  int32_t pole[2];                   /* Y:0x371/0x372, the one-pole's coefficients */
  int32_t coef[GM2_GATE_COEFS];      /* the live words, gliding to the config's */
  int32_t ramp_step[GM2_GATE_RAMPS]; /* each ramp's step, armed by a turn of its knob */
  int32_t raw[GM2_GATE_RAMPS];       /* the config's raw values at the last job run */
  unsigned chunk_pos;                /* frame within the chunk */
} gm2_gate_state;

void gm2_gate_state_init(gm2_gate_state *state, const gm2_gate_config *config);

void gm2_gate_process_split(gm2_gate_state *state, const gm2_gate_config *config,
                            const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                            int32_t *out_r, size_t frames);

void gm2_gate_process_f32(gm2_gate_state *state, const gm2_gate_config *config, const float *in_l,
                          const float *in_r, float *out_l, float *out_r, size_t frames);

void gm2_gate_process_interleaved(gm2_gate_state *state, const gm2_gate_config *config,
                                  const int32_t *in, int32_t *out, size_t frames);

void gm2_gate_process_frame(gm2_gate_state *state, const gm2_gate_config *config,
                            const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_GATE_H */
