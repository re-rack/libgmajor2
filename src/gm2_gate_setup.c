/* The Noise Gate's four updaters on the firmware's software float. With M, T, D
 * and R the raw values and rate the sample rate:
 *
 *   P:0x0904E8  mode       = 0.1875 + M * 0.4375
 *   P:0x090555  threshold  = T * {0x550A96,0x77}
 *   P:0x090513  damping    = D * {0xAAF56A,0x77}
 *   P:0x090582  step       = R * {0x550A96,0x77} / (rate/4)
 *               slow       = exp(-1 / (rate/4)), or exp(-1 / (10/R * rate/4))
 *                            above 10 dB/s
 *               fast       = exp(-1 / (0.0001 * rate / 4))
 *               blend      = (fast - slow) / {0x6A4D3C,0x7F}
 *               release    = {step, blend, slow, fast}
 */
#include "gm2_gate_setup.h"

#include "gm2_dsp_math.h"
#include "gm2_rate.h"
#include "gm2_swf.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  MODE_MIN = 0,
  MODE_MAX = 1,
  THRESHOLD_MIN = -61,
  THRESHOLD_MAX = 0,
  DAMPING_MIN = 0,
  DAMPING_MAX = 90,
  RELEASE_MIN = 3,
  RELEASE_MAX = 200,
};

static const gm2_swf k_mode_base = GM2_SWF_CONST(0x600000u, 0x7Du);  /* 0.1875 */
static const gm2_swf k_mode_step = GM2_SWF_CONST(0x700000u, 0x7Eu);  /* 0.4375 */
static const gm2_swf k_db = GM2_SWF_CONST(0x550A96u, 0x77u);         /* log2(gain)/64 per dB */
static const gm2_swf k_db_down = GM2_SWF_CONST(0xAAF56Au, 0x77u);    /* the same, negated */
static const gm2_swf k_fast_time = GM2_SWF_CONST(0x68DB8Cu, 0x72u);  /* 0.0001 */
static const gm2_swf k_blend_span = GM2_SWF_CONST(0x6A4D3Cu, 0x7Fu); /* 0.83 */

/* P:0x090582: the four Release words. */
static void gate_release(const gm2_rate_words *rate, int release, int32_t out[4]) {
  const gm2_swf speed = gm2_swf_from_int(release);
  const gm2_swf chunk_rate = gm2_swf_div(rate->rate, gm2_swf_four);
  const gm2_swf step = gm2_swf_div(gm2_swf_mul(speed, k_db), chunk_rate);
  gm2_swf slow = gm2_swf_exp(gm2_swf_div(gm2_swf_minus_one, chunk_rate));
  if (gm2_swf_cmp(speed, gm2_swf_ten) > 0) {
    const gm2_swf time =
        gm2_swf_div(gm2_swf_mul(gm2_swf_div(gm2_swf_ten, speed), rate->rate), gm2_swf_four);
    slow = gm2_swf_exp(gm2_swf_div(gm2_swf_minus_one, time));
  }
  const gm2_swf fast = gm2_swf_exp(gm2_swf_div(
      gm2_swf_minus_one, gm2_swf_div(gm2_swf_mul(k_fast_time, rate->rate), gm2_swf_four)));
  out[0] = gm2_swf_to_q23(step);
  out[1] = gm2_swf_to_q23(gm2_swf_div(gm2_swf_sub(fast, slow), k_blend_span));
  out[2] = gm2_swf_to_q23(slow);
  out[3] = gm2_swf_to_q23(fast);
}

int gm2_gate_setup_config(const gm2_context *context, const gm2_gate_setup_raw *raw,
                          gm2_gate_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }
  const int mode = gm2_clamp_raw(raw->mode, MODE_MIN, MODE_MAX);
  const int threshold = gm2_clamp_raw(raw->threshold, THRESHOLD_MIN, THRESHOLD_MAX);
  const int damping = gm2_clamp_raw(raw->damping, DAMPING_MIN, DAMPING_MAX);
  const int release = gm2_clamp_raw(raw->release, RELEASE_MIN, RELEASE_MAX);

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  config->coef[GM2_GATE_MODE] =
      gm2_swf_to_q23(gm2_swf_mac(k_mode_base, gm2_swf_from_int(mode), k_mode_step));
  config->coef[GM2_GATE_THRESHOLD] = gm2_swf_to_q23(gm2_swf_mul(gm2_swf_from_int(threshold), k_db));
  /* P:0x090513 clears the word when live parameter 15 is set, except for a
   * threshold of Off; no setting the port covers sets that flag. */
  config->coef[GM2_GATE_DAMPING] =
      gm2_swf_to_q23(gm2_swf_mul(gm2_swf_from_int(damping), k_db_down));
  gate_release(&rate_words, release, &config->coef[GM2_GATE_RELEASE]);

  config->raw[GM2_GATE_MODE] = mode;
  config->raw[GM2_GATE_THRESHOLD] = threshold;
  config->raw[GM2_GATE_DAMPING] = damping;
  return 0;
}
