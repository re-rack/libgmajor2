/* The pitch routine P:0x000D15..0x000E4D with its detector P:0x0003F2..0x00089C,
 * which the Detune, the Whammy, the Octaver, the Pitch Shifter and the
 * Intelligent Pitch Shifter run (Pitch block subtypes 0..4).
 *
 * Per stereo frame the routine writes the mono input and both voices' feedback
 * to a 20,000-word delay line. Each of the two voices reads the line through two
 * interpolated taps a splice apart. The taps drift at the voice's pitch ratio,
 * and when the first one leaves the voice's delay window a crossfade moves the
 * voice onto the second one, a splice further in. The pan matrix and the Mix
 * knob's wet and dry words turn the two voices and the input into left and right.
 *
 * On the first frame of each chunk the detector takes in the chunk's four mono
 * words, through a biquad and a one-pole, and runs one step of each of its two
 * searches: a coarse one over the whole period window, for the splice's length,
 * and a fine one around its last result, whose pitch the Intelligent Pitch
 * Shifter harmonizes. Once in a cycle of 32 chunks the detected period enters a
 * ring, from which each voice takes the period of its delay ago as its splice
 * length; each voice's delay takes a step towards the config's; and the taps'
 * drift and the crossfade's speed follow the ratios.
 *
 * The ratios, the feedbacks and the pan gains ride one-pole jobs (gm2_job.h),
 * and the two delays the routine's own steps.
 */
#ifndef GM2_PITCH_CORE_H
#define GM2_PITCH_CORE_H

#include <stdint.h>

#include "gm2_base.h"

#define GM2_PITCH_CORE_VOICES 2u
#define GM2_PITCH_CORE_CURVE_WORDS 37u /* three points a semitone, one octave and its end */
#define GM2_PITCH_CORE_AUDIO_WORDS 20000u
#define GM2_PITCH_CORE_DET_WORDS 2048u
#define GM2_PITCH_CORE_RING_WORDS 256u
#define GM2_PITCH_CORE_AMDF_WORDS 257u
#define GM2_PITCH_CORE_COARSE_WORDS 64u
#define GM2_PITCH_CORE_FINE_LAGS 7u
#define GM2_PITCH_CORE_FINE_WORDS 16u

typedef struct {
  int32_t feedback[GM2_PITCH_CORE_VOICES]; /* Y:0x021C/0x021D, staged at X:0x0987C6 */
  /* Y:0x0220..0x0223, staged at X:0x0987C8: voice 2 to the right, voice 2 to the
   * left, voice 1 to the left, voice 1 to the right */
  int32_t pan[2u * GM2_PITCH_CORE_VOICES];
  int32_t ratio[GM2_PITCH_CORE_VOICES]; /* X:0x041D/0x041E: each voice's pitch ratio less one */
  int32_t delay[GM2_PITCH_CORE_VOICES]; /* X:0x0412, X:0x0414: each voice's delay */
  int32_t rate_factor;                  /* X:0x0416: the crossfade's speed per unit of ratio */
  int32_t min_period;                   /* X:0x0566: the detector's period window */
  int32_t max_period;                   /* X:0x0567 */
  int32_t lag_count;                    /* X:0x0568: the lags its coarse search scans */
  int32_t wet;                          /* X:0x09884E: the Mix knob's wet word */
  int32_t dry;                          /* X:0x09884F: its dry word */

  /* The Intelligent Pitch Shifter's words. */
  int harmonize;      /* X:0x0423: 1 when the ratios follow the detected pitch */
  int32_t ref_offset; /* X:0x0424: the tuning reference and the key */
  int32_t
      curve_gain[GM2_PITCH_CORE_VOICES]; /* Y:0x0228, Y:0x022A: the ratio of each curve's mean */
  /* Y:0x022B..0x0274: each voice's interval over the note of the scale */
  int32_t curve[GM2_PITCH_CORE_VOICES][GM2_PITCH_CORE_CURVE_WORDS];
} gm2_pitch_core_config;

typedef struct {
  int32_t out;        /* X:0x0400 + v: the voice's output */
  int32_t fade;       /* X:0x0417 + 3v: the crossfade between the two taps, 0 down to -1 */
  int32_t offset;     /* X:0x0418 + 3v: the first tap's distance behind the write point */
  int32_t jump;       /* X:0x0419 + 3v: the second tap's distance from the first */
  int32_t delay;      /* X:0x0413, X:0x0415: the delay, stepping to the config's */
  int32_t fade_step;  /* Y:0x0208 + 5v: the crossfade's step per frame */
  int32_t span;       /* Y:0x0209 + 5v: the next splice's length */
  int32_t offset_min; /* Y:0x020A + 5v: the delay window the first tap stays in */
  int32_t offset_max; /* Y:0x020B + 5v */
  int32_t ring_tap;   /* Y:0x021A + v: how far back in the period ring the voice reads */
  int32_t rate;       /* Y:0x021E + v: the first tap's drift per frame */
} gm2_pitch_core_voice;

typedef struct {
  int32_t audio_buf[GM2_PITCH_CORE_AUDIO_WORDS]; /* X:0x088000..0x08CE1F, m2 = 0x4E1F */
  uint32_t audio_at;                             /* X:0x03FD: its write index */
  int32_t chunk[GM2_CHUNK_FRAMES * GM2_CHANNELS]; /* Y:0x0200..0x0207: the last four input frames */
  uint32_t chunk_at; /* X:0x0402: its write offset */
  gm2_pitch_core_voice voice[GM2_PITCH_CORE_VOICES];
  int32_t feedback[GM2_PITCH_CORE_VOICES]; /* the live words, gliding to the config's */
  int32_t pan[2u * GM2_PITCH_CORE_VOICES]; /* the same */
  int32_t ratio[GM2_PITCH_CORE_VOICES]; /* Y:0x0224/0x0225: the live ratios */
  int32_t wet; /* Y:0x03A1: the live pair, Y:0x03A2 the dry one */
  int32_t dry;
  int32_t harmony[GM2_PITCH_CORE_VOICES]; /* X:0x041D/0x041E as the harmonizer left them */
  int32_t period_ring[GM2_PITCH_CORE_RING_WORDS]; /* X:0x0800..0x08FF, m3 = 0xFF */
  uint32_t ring_at; /* Y:0x0219: its write index */
  int32_t cycle; /* X:0x0403: chunks until the ring's next word */
  int32_t period; /* X:0x0410: the last detected period in the window */

  /* The detector's input. */
  int32_t det_buf[GM2_PITCH_CORE_DET_WORDS]; /* X:0x082000..0x0827FF, m4 = 0x07FF */
  uint32_t det_at; /* X:0x042F: its write index */
  int32_t det_biquad[4]; /* X:0x0425..0x0428: x[n-1], x[n-2], y[n-1], y[n-2] */
  int32_t det_low_pass; /* X:0x042A */

  /* The coarse search: a template, one lag of its difference function a chunk,
   * then the peaks of that function and the period they point at. */
  uint32_t coarse_state; /* X:0x0432 */
  int32_t coarse_template[GM2_PITCH_CORE_COARSE_WORDS]; /* Y:0x027E..0x02BD */
  int32_t coarse_energy[2]; /* X:0x0433/0x0434: the template's, high and low */
  int32_t coarse_shift; /* X:0x0435: the shift that normalizes it */
  int32_t amdf[GM2_PITCH_CORE_AMDF_WORDS]; /* X:0x0464..0x0564: the difference function */
  uint32_t coarse_at; /* X:0x0430: the lag the scan is at */
  uint32_t coarse_end; /* X:0x0431: where it ends */
  int32_t peak_period; /* X:0x043D: the strongest peak's period */
  int32_t first_period; /* X:0x0578: the first good peak in the window */
  int32_t first_conf; /* X:0x0579: its height over the template's energy */
  int32_t near_period; /* X:0x057A: the peak nearest the locked period */
  int32_t locked_period; /* X:0x057C: the period the tracker holds */
  int32_t locked_count; /* X:0x057D: how settled it is */
  int32_t alt_period; /* X:0x057E: a rival period */
  int32_t alt_count; /* X:0x057F: how settled that is */
  int32_t tracked_period; /* X:0x0436: the tracker's answer */
  int32_t coarse_fresh; /* X:0x0572: set until the fine search has read it */

  /* The fine search: a short template and seven lags around the last estimate. */
  uint32_t fine_state; /* X:0x0569 */
  int32_t fine_template[GM2_PITCH_CORE_FINE_WORDS]; /* Y:0x02C5..0x02D4 */
  int32_t fine_score[GM2_PITCH_CORE_FINE_LAGS]; /* Y:0x02BE..0x02C4 */
  int32_t fine_lag; /* X:0x056A: the lag the scan is at, 6..0 */
  uint32_t fine_at; /* X:0x056B: its address in the detector buffer */
  int32_t fine_window; /* X:0x056C: how far back the template starts */
  int32_t fine_estimate; /* X:0x056E: the period the lags surround */
  int32_t fine_stride; /* X:0x056F: the words between two template points */
  int32_t fine_best_lag; /* X:0x0570 */
  int32_t fine_best_score; /* X:0x0571 */
  int32_t log_period; /* X:0x043A: -log2 of the fine period */

  unsigned chunk_pos; /* frame within the chunk */
} gm2_pitch_core_state;

void gm2_pitch_core_state_init(gm2_pitch_core_state *state, const gm2_pitch_core_config *config);

void gm2_pitch_core_process_frame(gm2_pitch_core_state *state, const gm2_pitch_core_config *config,
                                  const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_PITCH_CORE_H */
