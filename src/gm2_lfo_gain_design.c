/* The Tremolo/Panner updaters on the firmware's software float. With S the Speed
 * step, T the Tempo value, B the host's beat and D the Depth or Width:
 *
 *   P:0x0918CE  step     = gm2_lfo_rate(S, T, B) / rate
 *   P:0x091999  depth    = D * 0.01 * {0x7FFCB9,0x7F}
 *   P:0x0918AF  smoother = {pole, 1 - pole}, pole = exp(-100 / rate), a 10 ms
 *                          time constant
 *   P:0x08F763  wet      = the Out Level word times the full mix's sine half
 */
#include "gm2_lfo_gain_design.h"

#include "gm2_dsp_math.h"
#include "gm2_lfo.h"
#include "gm2_mix.h"
#include "gm2_swf.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  SPEED_MIN = 0,
  SPEED_MAX = GM2_LFO_SPEED_MAX,
  DEPTH_MIN = 0,
  DEPTH_MAX = 100,
  TEMPO_MIN = 0,
  TEMPO_MAX = GM2_LFO_NOTE_MAX,
};

static const gm2_swf k_depth_scale = GM2_SWF_CONST(0x7FFCB9u, 0x7Fu);  /* just below 1 */
static const gm2_swf k_smoother_rate = GM2_SWF_CONST(0x99999Au, 0x7Cu); /* -0.1 per ms */

void gm2_lfo_gain_design(const gm2_rate_words *rate, const gm2_lfo_gain_design_raw *raw,
                         gm2_lfo_gain_core_config *config) {
  const int speed = gm2_clamp_raw(raw->speed, SPEED_MIN, SPEED_MAX);
  const int depth = gm2_clamp_raw(raw->depth, DEPTH_MIN, DEPTH_MAX);
  const int tempo = gm2_clamp_raw(raw->tempo, TEMPO_MIN, TEMPO_MAX);

  config->note = (unsigned)tempo;
  config->coef[GM2_LFO_GAIN_CORE_STEP] =
      gm2_lfo_step(rate, gm2_lfo_rate(speed, config->note, raw->beat_ms));
  config->coef[GM2_LFO_GAIN_CORE_DEPTH] = gm2_swf_to_q23(
      gm2_swf_mul(gm2_swf_mul(gm2_swf_from_int(depth), gm2_swf_hundredth), k_depth_scale));

  const int32_t pole = gm2_swf_to_q23(gm2_swf_exp(gm2_swf_mul(rate->period_ms, k_smoother_rate)));
  config->smooth[0] = pole;
  config->smooth[1] = GM2_W24_MAX - pole;

  /* The level code P:0x08F763 stages the block's wet word for a full mix, and its
   * dry term is a zero word. */
  int32_t dry;
  gm2_mix_words(raw->out_level, 100, &config->wet, &dry);

  config->raw[GM2_LFO_GAIN_CORE_RAW_SPEED] = speed;
  config->raw[GM2_LFO_GAIN_CORE_RAW_TEMPO] = tempo;
  config->raw[GM2_LFO_GAIN_CORE_RAW_DEPTH] = depth;
}
