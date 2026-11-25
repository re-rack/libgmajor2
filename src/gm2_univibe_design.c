/* The Univibe parameter setup P:0x09311B on the firmware's software float: see
 * gm2_univibe_design.h.
 *
 * With A the Attack value, R the Release value and D the Depth value, the words
 * are
 *
 *   P:0x0932C2  bilinear(z) = (1 - tan(pi z)) / (1 + tan(pi z)), from the eight
 *               pairs X:0x098BB8..X:0x098BC7 (four stages, low and high word)
 *               curve(x)    = ((1 - x) / 2)^(1/3) with exponent {0x555556, 0x7E}
 *               the curves  base = curve(bilinear(low[k])),
 *                           slope = curve(bilinear(high[k])) - base
 *   P:0x0933CD  exp(-4000 / (rate * time_ms)) and its complement, the two words
 *               of a P:0x001072 slewer: the speed poles for A * 50 ms and
 *               R * 50 ms, the lamp's poles for 5 ms and 30 ms
 *   P:0x09339D  depth = min(to_q23(D * 0.005), 0x3FF972)
 *   P:0x093565  wet = -0x5A827B when Phase Reverse is on and 0x5A827B when it is
 *               off
 *   P:0x09349C  lfo_speed = to_q23(hz / rate * 1024), the phase step of the Speed
 *               the Select value picks, zeroed when that Speed value is 37 or less
 */
#include "gm2_univibe_design.h"

#include "gm2_dsp_math.h"
#include "gm2_lfo.h"
#include "gm2_swf.h"
#include "gm2_tables.h"

static const gm2_swf k_pi = GM2_SWF_CONST(0x6487EEu, 0x81u);
static const gm2_swf k_phase_scale = GM2_SWF_CONST(0x400000u, 0x88u); /* 256 */
static const gm2_swf k_cube_root = GM2_SWF_CONST(0x555556u, 0x7Eu);
static const gm2_swf k_time_step = GM2_SWF_CONST(0x640000u, 0x85u); /* 50 ms a step */
static const gm2_swf k_lamp_rise = GM2_SWF_CONST(0x500000u, 0x82u); /* 5 ms */
static const gm2_swf k_lamp_fall = GM2_SWF_CONST(0x780000u, 0x84u); /* 30 ms */
static const gm2_swf k_depth_step = GM2_SWF_CONST(0x51EB85u, 0x78u); /* 0.005 */

/* Y:0x0474/0x0475: the waveshaper's linear and square weights. The Vintage
 * Univibe squares the lamp drive; the Modern one adds a linear term and bends it
 * back. */
static const int32_t k_vintage_shaper[2] = {0, 0x0CCCCD};
static const int32_t k_modern_shaper[2] = {0x200000, -0x100000};

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  SPEED_MIN = 37,
  SPEED_MAX = 184,
  DEPTH_MIN = 0,
  DEPTH_MAX = 100,
  TEMPO_MIN = 0,
  TEMPO_MAX = GM2_LFO_NOTE_MAX,
  ATTACK_MIN = 1,
  ATTACK_MAX = 100,
  RELEASE_MIN = 1,
  RELEASE_MAX = 100,
  SELECT_MIN = 0,
  SELECT_MAX = 1,
  PHASE_REVERSE_MIN = 0,
  PHASE_REVERSE_MAX = 1,
};

/* The Depth word's ceiling and the Speed value that stops the LFO. */
enum {
  UNIVIBE_DESIGN_DEPTH_WORD_MAX = 0x3FF972,
  UNIVIBE_DESIGN_SPEED_STOPPED = 0x25,
};

/* The wet weight: 2^-0.5. */
enum { UNIVIBE_DESIGN_WET_WEIGHT = 0x5A827B };

/* P:0x093276: the bilinear pole (1 - tan(pi x)) / (1 + tan(pi x)). */
static gm2_swf univibe_bilinear(gm2_swf x) {
  const gm2_swf inverse = gm2_swf_div(gm2_swf_one, gm2_swf_tan(gm2_swf_mul(x, k_pi)));
  return gm2_swf_div(gm2_swf_sub(inverse, gm2_swf_one), gm2_swf_add(inverse, gm2_swf_one));
}

/* P:0x0932AE: ((1 - x) / 2)^(1/3). */
static gm2_swf univibe_curve(gm2_swf x) {
  return gm2_swf_pow(gm2_swf_div(gm2_swf_sub(gm2_swf_one, x), gm2_swf_two), k_cube_root);
}

/* P:0x0933CD: exp(-4000 / (rate * time_ms)) into `out_keep` and its complement
 * into `out_new`, the pair a P:0x001072 slewer weights the target and the live
 * word with. */
static void univibe_pole_pair(const gm2_rate_words *rate, gm2_swf time_ms, int32_t *out_new,
                              int32_t *out_keep) {
  const gm2_swf arg = gm2_swf_div(gm2_swf_mul(gm2_swf_neg(rate->period_ms), gm2_swf_four), time_ms);
  const gm2_swf pole = gm2_swf_exp(arg);
  *out_keep = gm2_swf_to_q23(pole);
  *out_new = gm2_swf_to_q23(gm2_swf_sub(gm2_swf_one, pole));
}

/* P:0x09349C: the phase step words Y:0x046C the frame advances its phase with,
 * from a Speed value or from a Tempo value at the host's beat. */
static int32_t univibe_speed_step(const gm2_rate_words *rate, int speed, int tempo,
                                  double beat_ms) {
  const gm2_swf hz = gm2_lfo_rate(speed, (unsigned)tempo, beat_ms);
  int32_t effective = speed;
  if (tempo != 0) {
    effective = gm2_lfo_rate_to_speed(hz);
  }
  gm2_swf step = gm2_swf_mul(hz, rate->period);
  step = gm2_swf_mul(step, k_phase_scale);
  step = gm2_swf_mul(step, gm2_swf_four);
  if ((uint32_t)effective <= (uint32_t)UNIVIBE_DESIGN_SPEED_STOPPED) {
    return 0;
  }
  return gm2_swf_to_q23(step);
}

void gm2_univibe_design(const gm2_rate_words *rate, gm2_univibe_design_subtype subtype,
                        const gm2_univibe_design_raw *raw, gm2_univibe_core_config *config) {
  const int speed_1 = gm2_clamp_raw(raw->speed_1, SPEED_MIN, SPEED_MAX);
  const int speed_2 = gm2_clamp_raw(raw->speed_2, SPEED_MIN, SPEED_MAX);
  const int depth = gm2_clamp_raw(raw->depth, DEPTH_MIN, DEPTH_MAX);
  const int tempo_1 = gm2_clamp_raw(raw->tempo_1, TEMPO_MIN, TEMPO_MAX);
  const int tempo_2 = gm2_clamp_raw(raw->tempo_2, TEMPO_MIN, TEMPO_MAX);
  const int attack = gm2_clamp_raw(raw->attack, ATTACK_MIN, ATTACK_MAX);
  const int release = gm2_clamp_raw(raw->release, RELEASE_MIN, RELEASE_MAX);
  const int select = gm2_clamp_raw(raw->select, SELECT_MIN, SELECT_MAX);
  const int phase_reverse = gm2_clamp_raw(raw->phase_reverse, PHASE_REVERSE_MIN, PHASE_REVERSE_MAX);
  const unsigned note = (unsigned)(select != 0 ? tempo_2 : tempo_1);

  /* P:0x0932C2: each stage's base word and the slope the lamp brightness adds to
   * it, both from the stage's low and high frequency pair. */
  for (unsigned k = 0; k < GM2_UNIVIBE_CORE_STAGES; k++) {
    config->coef[GM2_UNIVIBE_CORE_CURVES + 2u * k + 1u] =
        gm2_swf_to_q23(univibe_curve(univibe_bilinear(gm2_univibe_ap_table[k])));
  }
  for (unsigned k = 0; k < GM2_UNIVIBE_CORE_STAGES; k++) {
    const gm2_swf high = univibe_curve(univibe_bilinear(gm2_univibe_ap_table[4u + k]));
    const gm2_swf low = gm2_swf_from_q23(config->coef[GM2_UNIVIBE_CORE_CURVES + 2u * k + 1u]);
    config->coef[GM2_UNIVIBE_CORE_CURVES + 2u * k] = gm2_swf_to_q23(gm2_swf_sub(high, low));
  }

  const int32_t *shaper =
      subtype == GM2_UNIVIBE_DESIGN_VINTAGE ? k_vintage_shaper : k_modern_shaper;
  config->shaper[0] = shaper[0];
  config->shaper[1] = shaper[1];

  /* P:0x0933CD: the Attack and Release slew times, then the two lamp filament
   * poles the routine's own slewer reads. */
  univibe_pole_pair(rate, gm2_swf_mul(gm2_swf_from_int(attack), k_time_step),
                    &config->coef[GM2_UNIVIBE_CORE_SPEED_POLES + 2],
                    &config->coef[GM2_UNIVIBE_CORE_SPEED_POLES + 3]);
  univibe_pole_pair(rate, gm2_swf_mul(gm2_swf_from_int(release), k_time_step),
                    &config->coef[GM2_UNIVIBE_CORE_SPEED_POLES + 0],
                    &config->coef[GM2_UNIVIBE_CORE_SPEED_POLES + 1]);
  univibe_pole_pair(rate, k_lamp_rise, &config->coef[GM2_UNIVIBE_CORE_LAMP_POLES + 2],
                    &config->coef[GM2_UNIVIBE_CORE_LAMP_POLES + 3]);
  univibe_pole_pair(rate, k_lamp_fall, &config->coef[GM2_UNIVIBE_CORE_LAMP_POLES + 0],
                    &config->coef[GM2_UNIVIBE_CORE_LAMP_POLES + 1]);

  /* P:0x09339D: the Depth word, at most 0x3FF972. */
  int32_t depth_word = gm2_swf_to_q23(gm2_swf_mul(gm2_swf_from_int(depth), k_depth_step));
  if (depth_word > UNIVIBE_DESIGN_DEPTH_WORD_MAX) {
    depth_word = UNIVIBE_DESIGN_DEPTH_WORD_MAX;
  }
  config->coef[GM2_UNIVIBE_CORE_DEPTH] = depth_word;
  config->raw[GM2_UNIVIBE_CORE_DEPTH] = depth;

  /* P:0x093565: Phase Reverse flips the wet word's sign. */
  config->coef[GM2_UNIVIBE_CORE_WET] =
      phase_reverse != 0 ? -UNIVIBE_DESIGN_WET_WEIGHT : UNIVIBE_DESIGN_WET_WEIGHT;

  /* P:0x09349C: the two speeds' steps and the one Select picks. */
  const int32_t step_1 = univibe_speed_step(rate, speed_1, tempo_1, raw->beat_ms);
  const int32_t step_2 = univibe_speed_step(rate, speed_2, tempo_2, raw->beat_ms);
  config->lfo_speed = select != 0 ? step_2 : step_1;
  config->note = note;
}
