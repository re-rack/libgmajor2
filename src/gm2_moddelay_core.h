/* The chorus routine P:0x001117..0x0011F8, which the nine chorus, flanger and
 * vibrato effects run (Modulation block subtypes 0..8).
 *
 * Per stereo frame the routine takes each input word through the block's input
 * gain and a one-pole Hi-Cut, adds the previous frame's voice sums through the
 * feedback word and the feedback's low-pass and high-pass, and writes the result
 * to a 4,096-word delay line a channel. An LFO phase advances by its step; six
 * voices, three a channel, each read their own wave table at it. A voice reads
 * the line at its offset plus its depth times its LFO word, through two taps
 * that crossfade when the offset changes. The voices' sums run through an output
 * biquad (gm2_biquad.h) and the block's wet word.
 *
 * The thirty words below ride one-pole jobs, each voice's depth is taken in at
 * its LFO's trough, and each voice's offset moves by a tap crossfade
 * (gm2_job.h).
 */
#ifndef GM2_MODDELAY_CORE_H
#define GM2_MODDELAY_CORE_H

#include <stdint.h>

#include "gm2_base.h"
#include "gm2_biquad.h"
#include "gm2_job.h"

#define GM2_MODDELAY_CORE_VOICES 6u
#define GM2_MODDELAY_CORE_TAPS 2u
#define GM2_MODDELAY_CORE_BUFFER_WORDS 4096u /* X:0x080000 and X:0x081000, m0 = m3 = 0x0FFF */
#define GM2_MODDELAY_CORE_COEFS 30u

/* The positions in coef[], the order of the staged words X:0x098BD1..0x098BEE. */
enum {
  GM2_MODDELAY_CORE_HI_CUT = 0,      /* Y:0x04AD..0x04B0: each channel's 1 - pole, pole */
  /* Y:0x04B1..0x04B8, four words a channel: the feedback low-pass's pole, the
   * high-pass's pole, and each one's input word */
  GM2_MODDELAY_CORE_FB_FILTER = 4,
  GM2_MODDELAY_CORE_LFO_STEP = 12,   /* Y:0x04B9 */
  GM2_MODDELAY_CORE_FEEDBACK = 13,   /* Y:0x04F4 */
  GM2_MODDELAY_CORE_OUT_BIQUAD = 14, /* Y:0x04EA..0x04F3: each channel's output biquad */
  GM2_MODDELAY_CORE_VOICE_GAIN = 24, /* Y:0x04E4..0x04E9 */
};

/* The wave tables P:0x093820 generates for the six voices. */
typedef enum {
  GM2_MODDELAY_CORE_LFO_UNIFORM,        /* every voice on the sine table */
  GM2_MODDELAY_CORE_LFO_TRICHORUS,      /* Tri-Chorus Normal's phase offsets */
  GM2_MODDELAY_CORE_LFO_TRICHORUS_ASYM, /* Tri-Chorus Asymmetric's */
} gm2_moddelay_core_lfo_set;

typedef struct {
  int32_t coef[GM2_MODDELAY_CORE_COEFS];        /* the words the updaters stage */
  int32_t depth[GM2_MODDELAY_CORE_VOICES];      /* Y:0x04BA + 5v: each voice's depth */
  int32_t
      tap_offset[GM2_MODDELAY_CORE_VOICES]; /* X:0x098C08 + 11v: each voice's delay in samples */
  gm2_moddelay_core_lfo_set lfo_set;
  unsigned note; /* the Tempo value the step follows, 0 for Speed */
} gm2_moddelay_core_config;

typedef struct {
  int32_t buf[GM2_CHANNELS][GM2_MODDELAY_CORE_BUFFER_WORDS];
  uint32_t write;                     /* Y:0x04A1/0x04A2: both lines' write index */
  int32_t hi_cut[GM2_CHANNELS];       /* X:0x02F0/0x02F1: the Hi-Cut's states */
  int32_t lfo_phase;                  /* X:0x02F2 */
  int32_t sum[GM2_CHANNELS]; /* X:0x0305/0x0306: the voices' sums, the feedback's source */
  int32_t fb_filter[GM2_CHANNELS][2]; /* X:0x0307..0x030A: the feedback low-pass and high-pass */
  int32_t out_in[GM2_CHANNELS][2]; /* X:0x006C/0x006D and X:0x0070/0x0071: x[n-1], x[n-2] */
  gm2_biquad out_biquad[GM2_CHANNELS]; /* L:0x006E, X:0x006F and L:0x0072, X:0x0073 */
  int32_t coef[GM2_MODDELAY_CORE_COEFS]; /* the live words, gliding to the config's */
  int32_t depth[GM2_MODDELAY_CORE_VOICES]; /* Y:0x04BB + 5v: the depth each voice runs at */
  int32_t tap_offset[GM2_MODDELAY_CORE_VOICES][GM2_MODDELAY_CORE_TAPS]; /* Y:0x04BC/0x04BE + 5v */
  int32_t tap_gain[GM2_MODDELAY_CORE_VOICES][GM2_MODDELAY_CORE_TAPS]; /* Y:0x04D8/0x04D9 + 2v */
  gm2_job_fade tap_fade[GM2_MODDELAY_CORE_VOICES]; /* X:0x098C01 + 11v */
  unsigned chunk_pos; /* frame within the chunk */
} gm2_moddelay_core_state;

void gm2_moddelay_core_state_init(gm2_moddelay_core_state *state,
                                  const gm2_moddelay_core_config *config);

void gm2_moddelay_core_process_frame(gm2_moddelay_core_state *state,
                                     const gm2_moddelay_core_config *config,
                                     const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

void gm2_moddelay_core_align(gm2_moddelay_core_state *state, const gm2_moddelay_core_config *config,
                             double beats);

#endif /* GM2_MODDELAY_CORE_H */
