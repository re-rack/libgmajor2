/* G-Major 2 Compressor (algorithm 0): the kernel P:0x00125E..0x0012D1.
 *
 * Per stereo frame the kernel multiplies both input words by the block's input
 * gain, smooths the square of the louder one into a 48-bit detector level,
 * applies the gain word to both channels with five bits of headroom, and applies
 * the block's wet word.
 *
 * On the first frame of each chunk it also runs the gain computer: log2 of the
 * detector level, the threshold, a soft knee and the ratio, a target that creeps
 * up by the release step while the gain recovers, a 48-bit smoothed log2 gain
 * whose coefficient moves between a slow and a fast pole with the gap, the makeup
 * gain, and exp2 back into the gain word the next frames use.
 *
 * Threshold, Ratio and Gain ride ramps and the other eight words a one-pole job
 * (gm2_job.h).
 */
#ifndef GM2_COMP_H
#define GM2_COMP_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_base.h"

#define GM2_COMP_RAMPS 3u
#define GM2_COMP_TIME_COEFS 3u
#define GM2_COMP_COEFS (GM2_COMP_RAMPS + 2u + 2u * GM2_COMP_TIME_COEFS)

/* The positions in coef[]. The first GM2_COMP_RAMPS ramp, and raw[] holds their
 * knobs' values at the same positions. */
enum {
  GM2_COMP_THRESHOLD,    /* Y:0x56A, staged at X:0x098F13 by P:0x094D5C */
  GM2_COMP_RATIO,        /* Y:0x56C, staged at X:0x098F14 by P:0x094D5C */
  GM2_COMP_GAIN,         /* Y:0x575, staged at X:0x098F15 by P:0x094D5C: the makeup gain */
  GM2_COMP_DETECTOR,     /* Y:0x567, staged at X:0x098F12 by P:0x094E18: the detector's pole */
  GM2_COMP_RELEASE_STEP, /* Y:0x56D, staged at X:0x098F0B by P:0x094E18 */
  /* Y:0x56E..0x570 and Y:0x571..0x573, staged at X:0x098F0C..0x098F11 by
   * P:0x094E18: a blend, a slow pole and a fast pole each. The gain computer reads
   * the release words while the gain recovers and the attack words while it
   * falls. */
  GM2_COMP_RELEASE,
  GM2_COMP_ATTACK = GM2_COMP_RELEASE + GM2_COMP_TIME_COEFS,
};

typedef struct {
  int32_t coef[GM2_COMP_COEFS]; /* the words the updaters stage */
  int32_t raw[GM2_COMP_RAMPS];  /* the raw values of the knobs whose turns arm a ramp */
} gm2_comp_config;

typedef struct {
  int64_t level;                     /* L:0x00D, the detector level, signed 48-bit */
  int32_t gain;                      /* X:0x379, the gain word the audio path applies */
  int32_t log_gain[2];               /* X:0x37A/0x37B, the smoothed log2 gain, high and low */
  int32_t target;                    /* X:0x37C, the value the log2 gain follows */
  int32_t coef[GM2_COMP_COEFS];      /* the live words, gliding to the config's */
  int32_t ramp_step[GM2_COMP_RAMPS]; /* each ramp's step, armed by a turn of its knob */
  int32_t raw[GM2_COMP_RAMPS];       /* the config's raw values at the last job run */
  unsigned chunk_pos;                /* frame within the chunk */
} gm2_comp_state;

void gm2_comp_state_init(gm2_comp_state *state, const gm2_comp_config *config);

void gm2_comp_process_split(gm2_comp_state *state, const gm2_comp_config *config,
                            const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                            int32_t *out_r, size_t frames);

void gm2_comp_process_f32(gm2_comp_state *state, const gm2_comp_config *config, const float *in_l,
                          const float *in_r, float *out_l, float *out_r, size_t frames);

void gm2_comp_process_interleaved(gm2_comp_state *state, const gm2_comp_config *config,
                                  const int32_t *in, int32_t *out, size_t frames);

void gm2_comp_process_frame(gm2_comp_state *state, const gm2_comp_config *config,
                            const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_COMP_H */
