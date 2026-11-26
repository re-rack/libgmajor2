/* The Modulation block's parameter setup P:0x0938BE on the firmware's software
 * float: see gm2_moddelay_design.h.
 *
 * With S the Speed value, D the Depth value, T the Tempo value, H the Hi-Cut,
 * FB the Feedback, FC the Feedback Hi-Cut, L the Delay, W the Width, G the Golden
 * Ratio value, P the Phase Reverse value and B the host's beat in milliseconds,
 * the routines are
 *
 *   P:0x093884  S+24..29  the six voice tap gains, one set per subtype group
 *                         (chorus/flanger/vibrato, Tri-Chorus Normal, Tri-Chorus
 *                         Asymmetric, TZF Negative, TZF Positive)
 *   P:0x093C85  S+12      hz = rate(S, T, B), the phase step
 *   P:0x093B64  depth[0..5]  g = min(1, max(0.03, 1 - hz * 0.05)) when G is on
 *                         and the subtype has the parameter, else 1;
 *                         subtypes 4..7 look their voice words up in the depth
 *                         tables and scale by 1e-4, the others stage
 *                         to_q23(D * 0.01 * g) on voices 0 and 3, the Vibrato
 *                         after multiplying by 0.2
 *   P:0x093CB7  S+14..23  five output biquad words per channel from
 *                         X:0x0030..0x003E, both channels sharing one set while
 *                         Phase Reverse is off; the Classic Chorus, Classic
 *                         Flanger and Vibrato have no parameter and leave it on
 *   P:0x093D5C  tap_offset[0..5]
 *                         Advanced Chorus and Flanger: to_int(rate_khz * max(0.1,
 *                         L * 0.1) ms) on voices 0 and 3,
 *                         Classic Flanger: to_int(rate_khz * 5 ms),
 *                         Tri-Chorus: to_int(rate_khz * table(W) * 0.1 ms) per
 *                         voice, TZF: 4 samples, else 10 ms on voices 0 and 3
 *   P:0x093EA1  S+13      to_q23(FB * 0.01) on the flangers, to_q23(FB * 0.005)
 *                         on the TZF ones, 0 elsewhere
 *   P:0x093EF9  S+4..11   p = exp(-2 pi FC_hz / rate) and h = exp(-2 pi 200 /
 *                         rate), {to_q23(p), to_q23(h), to_q23(1 - p),
 *                         to_q23(h - 1)} for the left and right channels
 *   P:0x093F60  S+0..3    q = exp(-2 pi H_hz / rate),
 *                         {to_q23(1 - q), to_q23(q)} for left and right
 *
 * The hidden slots keep the values the device holds for them: P:0x0832CB clamps
 * the Feedback, Feedback Hi-Cut and Depth slots to zero and the Delay, Golden
 * Ratio and Phase Reverse slots to zero where the subtype has no such parameter.
 * Only the Tap Time at X:0x099A66 and the beat at X:0x099A6B are the caller's.
 */
#include "gm2_moddelay_design.h"

#include "gm2_dsp_math.h"
#include "gm2_lfo.h"
#include "gm2_mix.h"
#include "gm2_swf.h"
#include "gm2_tables.h"

#include <stddef.h>
#include <stdint.h>

/* The firmware's operands as (mantissa, exponent) words. */
static const gm2_swf k_semi = GM2_SWF_CONST(0x51EB85u, 0x78u);       /* 0.005 */
static const gm2_swf k_gold_step = GM2_SWF_CONST(0x666667u, 0x7Bu);  /* 0.05 */
static const gm2_swf k_gold_floor = GM2_SWF_CONST(0x7AE148u, 0x7Au); /* 0.03 */
static const gm2_swf k_depth_step = GM2_SWF_CONST(0x68DB8Cu, 0x72u); /* 0.0001 */
static const gm2_swf k_delay_step = GM2_SWF_CONST(0x666666u, 0x7Cu); /* 0.1 ms */
static const gm2_swf k_delay_5ms = GM2_SWF_CONST(0x500000u, 0x82u); /* 5 */
static const gm2_swf k_delay_10ms = GM2_SWF_CONST(0x500000u, 0x83u); /* 10 */
static const gm2_swf k_feedback_hz = GM2_SWF_CONST(0x640000u, 0x87u); /* 200 Hz */

enum {
  MODDELAY_DESIGN_VIBRATO_DEPTH_WORD = 0x19999A, /* 0.2 as a 1.23 word */
  MODDELAY_DESIGN_DEPTH_COLUMNS = 4,             /* the Tri-Chorus depth tables' x row */
  MODDELAY_DESIGN_DEPTH_TZF_COLUMNS = 3,         /* the TZF depth tables' x row */
  MODDELAY_DESIGN_WIDTH_COLUMNS = 3,             /* the Tri-Chorus width tables' x row */
  MODDELAY_DESIGN_TZF_TAP_OFFSET = 4,            /* P:0x093D5C's fixed Through-Zero tap */
  MODDELAY_DESIGN_HI_CUT_HIDDEN = 45,            /* the device's hidden Hi-Cut value */
};

/* The descriptor windows P:0x0832CB clamps these raw values into. */
enum {
  SPEED_MIN = 0,
  SPEED_MAX = GM2_LFO_SPEED_MAX,
  DEPTH_MIN = 0,
  DEPTH_MAX = 100,
  TEMPO_MIN = 0,
  TEMPO_MAX = GM2_LFO_NOTE_MAX,
  HI_CUT_MIN = 0,
  HI_CUT_MAX = 60,
  FEEDBACK_MIN = -100,
  FEEDBACK_MAX = 100,
  FB_CUT_MIN = 0,
  FB_CUT_MAX = 60,
  DELAY_MIN = 1,
  DELAY_MAX = 500,
  WIDTH_MIN = 1,
  WIDTH_MAX = 100,
  GOLDEN_RATIO_MIN = 0,
  GOLDEN_RATIO_MAX = 1,
  PHASE_REVERSE_MIN = 0,
  PHASE_REVERSE_MAX = 1,
};

/* P:0x093A98: x0 + ((y0 - x0) / n2) * n5. */
static int32_t moddelay_interp(int32_t x0, int32_t y0, int32_t n2, int32_t n5) {
  const gm2_swf slope =
      gm2_swf_div(gm2_swf_sub(gm2_swf_from_int(y0), gm2_swf_from_int(x0)), gm2_swf_from_int(n2));
  return gm2_swf_to_int(gm2_swf_mac(gm2_swf_from_int(x0), slope, gm2_swf_from_int(n5)));
}

/* The pair of the table's x row that `value` falls inside, or the last pair when
 * it falls inside none, as P:0x093AC5 and P:0x093B1B walk it. */
static void moddelay_segment(const int32_t *table, unsigned columns, int32_t value, unsigned *lower,
                             unsigned *upper) {
  for (unsigned next = 1u; next + 1u < columns; next++) {
    if (value >= table[next - 1u] && value <= table[next]) {
      *lower = next - 1u;
      *upper = next;
      return;
    }
  }
  *lower = columns - 2u;
  *upper = columns - 1u;
}

/* P:0x093AC5: one voice's depth table word for the Depth value. */
static int32_t moddelay_depth_word(int32_t value, gm2_moddelay_design_subtype subtype,
                                   unsigned voice) {
  const int tzf =
      subtype == GM2_MODDELAY_DESIGN_TZFLANGER_NEG || subtype == GM2_MODDELAY_DESIGN_TZFLANGER_POS;
  const int32_t *table = tzf ? gm2_moddelay_depth_tzf_table
                         : subtype == GM2_MODDELAY_DESIGN_TRICHORUS_ASYM
                             ? gm2_moddelay_depth_asym_table
                             : gm2_moddelay_depth_norm_table;
  const unsigned columns = tzf ? MODDELAY_DESIGN_DEPTH_TZF_COLUMNS : MODDELAY_DESIGN_DEPTH_COLUMNS;
  unsigned lower, upper;
  moddelay_segment(table, columns, value, &lower, &upper);
  const int32_t base = table[lower];
  const unsigned row = (voice + 1u) * columns;
  return moddelay_interp(table[row + lower], table[row + upper], table[upper] - base, value - base);
}

/* P:0x093B1B: one voice's width table word for the Width value. */
static int32_t moddelay_width_word(int32_t value, gm2_moddelay_design_subtype subtype,
                                   unsigned voice) {
  const int32_t *table = subtype == GM2_MODDELAY_DESIGN_TRICHORUS_ASYM
                             ? gm2_moddelay_width_asym_table
                             : gm2_moddelay_width_norm_table;
  unsigned lower, upper;
  moddelay_segment(table, MODDELAY_DESIGN_WIDTH_COLUMNS, value, &lower, &upper);
  const int32_t base = table[lower];
  const unsigned row = (voice + 1u) * MODDELAY_DESIGN_WIDTH_COLUMNS;
  return moddelay_interp(table[row + lower], table[row + upper], table[upper] - base, value - base);
}

/* The Golden Ratio scale P:0x093B64 applies to the Depth value. */
static gm2_swf moddelay_golden_scale(gm2_swf hz) {
  const gm2_swf product = gm2_swf_msub(gm2_swf_one, hz, k_gold_step);
  if (gm2_swf_cmp(product, k_gold_floor) < 0) {
    return k_gold_floor;
  }
  return gm2_swf_cmp(product, gm2_swf_one) > 0 ? gm2_swf_one : product;
}

/* The samples `milliseconds` of delay span at this rate, as P:0x093D5C converts
 * them. */
static int32_t moddelay_tap_samples(gm2_swf milliseconds, const gm2_rate_words *rate) {
  return gm2_swf_to_int(gm2_swf_mul(rate->rate_khz, milliseconds));
}

void gm2_moddelay_design(const gm2_rate_words *rate, gm2_moddelay_design_subtype subtype,
                         const gm2_moddelay_design_raw *raw, gm2_moddelay_core_config *config) {
  const int tri_chorus =
      subtype == GM2_MODDELAY_DESIGN_TRICHORUS || subtype == GM2_MODDELAY_DESIGN_TRICHORUS_ASYM;
  const int tz_flanger =
      subtype == GM2_MODDELAY_DESIGN_TZFLANGER_NEG || subtype == GM2_MODDELAY_DESIGN_TZFLANGER_POS;
  const int flanger = subtype == GM2_MODDELAY_DESIGN_CFLANGER ||
                      subtype == GM2_MODDELAY_DESIGN_AFLANGER || tz_flanger;
  const int advanced =
      subtype == GM2_MODDELAY_DESIGN_ACHORUS || subtype == GM2_MODDELAY_DESIGN_AFLANGER;

  const int speed = gm2_clamp_raw(raw->speed, SPEED_MIN, SPEED_MAX);
  const int depth = gm2_clamp_raw(raw->depth, DEPTH_MIN, DEPTH_MAX);
  const int tempo = gm2_clamp_raw(raw->tempo, TEMPO_MIN, TEMPO_MAX);
  const int hi_cut = tri_chorus || tz_flanger ? MODDELAY_DESIGN_HI_CUT_HIDDEN
                                              : gm2_clamp_raw(raw->hi_cut, HI_CUT_MIN, HI_CUT_MAX);
  const int feedback = gm2_clamp_raw(raw->feedback, FEEDBACK_MIN, FEEDBACK_MAX);
  const int fb_cut = gm2_clamp_raw(raw->fb_cut, FB_CUT_MIN, FB_CUT_MAX);
  const int golden_ratio = gm2_clamp_raw(raw->golden_ratio, GOLDEN_RATIO_MIN, GOLDEN_RATIO_MAX);
  const int phase_reverse =
      subtype == GM2_MODDELAY_DESIGN_CCHORUS || subtype == GM2_MODDELAY_DESIGN_CFLANGER ||
              subtype == GM2_MODDELAY_DESIGN_VIBRATO
          ? 1
          : gm2_clamp_raw(raw->phase_reverse, PHASE_REVERSE_MIN, PHASE_REVERSE_MAX);

  /* P:0x093884: the six voice tap gains, one set per subtype group. */
  const unsigned gain_set = subtype == GM2_MODDELAY_DESIGN_TRICHORUS        ? 1u
                            : subtype == GM2_MODDELAY_DESIGN_TRICHORUS_ASYM ? 2u
                            : subtype == GM2_MODDELAY_DESIGN_TZFLANGER_NEG  ? 3u
                            : subtype == GM2_MODDELAY_DESIGN_TZFLANGER_POS  ? 4u
                                                                            : 0u;
  for (unsigned voice = 0; voice < GM2_MODDELAY_CORE_VOICES; voice++) {
    config->coef[GM2_MODDELAY_CORE_VOICE_GAIN + voice] = gm2_moddelay_voice_gains[gain_set][voice];
  }

  /* P:0x093C85: Speed and Tempo give the phase step. */
  const gm2_swf hz = gm2_lfo_rate(speed, (unsigned)tempo, raw->beat_ms);
  config->coef[GM2_MODDELAY_CORE_LFO_STEP] = gm2_lfo_step(rate, hz);

  /* P:0x093B64: Depth, scaled by Golden Ratio where the subtype has it. */
  const gm2_swf gold = advanced && golden_ratio != 0 ? moddelay_golden_scale(hz) : gm2_swf_one;
  for (unsigned voice = 0; voice < GM2_MODDELAY_CORE_VOICES; voice++) {
    config->depth[voice] = 0;
  }
  if (tri_chorus || tz_flanger) {
    for (unsigned voice = 0; voice < GM2_MODDELAY_CORE_VOICES; voice++) {
      const gm2_swf word =
          gm2_swf_mul(gm2_swf_from_int(moddelay_depth_word(depth, subtype, voice)), k_depth_step);
      config->depth[voice] = gm2_swf_to_q23(word);
    }
  } else {
    gm2_swf scaled = gm2_swf_mul(gm2_swf_mul(gm2_swf_from_int(depth), gm2_swf_hundredth), gold);
    int32_t word = gm2_swf_to_q23(scaled);
    if (subtype == GM2_MODDELAY_DESIGN_VIBRATO) {
      word = gm2_store24(gm2_mpyr(MODDELAY_DESIGN_VIBRATO_DEPTH_WORD, word));
    }
    config->depth[0] = word;
    config->depth[3] = word;
  }

  /* P:0x093CB7: the output biquad words, five per channel; with Phase Reverse
   * off both channels take the same five. */
  if (phase_reverse != 0) {
    for (unsigned i = 0; i < 2u * GM2_BIQUAD_COEFS; i++) {
      config->coef[GM2_MODDELAY_CORE_OUT_BIQUAD + i] =
          gm2_moddelay_pharev_table[GM2_BIQUAD_COEFS + i];
    }
  } else {
    for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
      for (unsigned i = 0; i < GM2_BIQUAD_COEFS; i++) {
        config->coef[GM2_MODDELAY_CORE_OUT_BIQUAD + GM2_BIQUAD_COEFS * channel + i] =
            gm2_moddelay_pharev_table[i];
      }
    }
  }

  /* P:0x093D5C: the delay each voice's taps read. */
  for (unsigned voice = 0; voice < GM2_MODDELAY_CORE_VOICES; voice++) {
    config->tap_offset[voice] = 0;
  }
  if (advanced) {
    const int delay = gm2_clamp_raw(raw->delay, DELAY_MIN, DELAY_MAX);
    const gm2_swf milliseconds = gm2_swf_mul(gm2_swf_from_int(delay), k_delay_step);
    config->tap_offset[0] = moddelay_tap_samples(milliseconds, rate);
    config->tap_offset[3] = config->tap_offset[0];
  } else if (tri_chorus) {
    const int width = gm2_clamp_raw(raw->width, WIDTH_MIN, WIDTH_MAX);
    for (unsigned voice = 0; voice < GM2_MODDELAY_CORE_VOICES; voice++) {
      const gm2_swf milliseconds =
          gm2_swf_mul(gm2_swf_from_int(moddelay_width_word(width, subtype, voice)), k_delay_step);
      config->tap_offset[voice] = moddelay_tap_samples(milliseconds, rate);
    }
  } else if (tz_flanger) {
    for (unsigned voice = 0; voice < GM2_MODDELAY_CORE_VOICES; voice++) {
      config->tap_offset[voice] = MODDELAY_DESIGN_TZF_TAP_OFFSET;
    }
  } else {
    const gm2_swf milliseconds =
        subtype == GM2_MODDELAY_DESIGN_CFLANGER ? k_delay_5ms : k_delay_10ms;
    config->tap_offset[0] = moddelay_tap_samples(milliseconds, rate);
    config->tap_offset[3] = config->tap_offset[0];
  }

  /* P:0x093EA1: the feedback word. */
  if (flanger) {
    const gm2_swf step = tz_flanger ? k_semi : gm2_swf_hundredth;
    config->coef[GM2_MODDELAY_CORE_FEEDBACK] =
        gm2_swf_to_q23(gm2_swf_mul(gm2_swf_from_int(feedback), step));
  } else {
    config->coef[GM2_MODDELAY_CORE_FEEDBACK] = 0;
  }

  /* P:0x093EF9: the feedback Hi-Cut pole and the fixed 200 Hz high-pass. */
  const gm2_swf fb_pole = gm2_swf_exp(gm2_swf_mul(gm2_swf_hz20(fb_cut), rate->minus_omega));
  const gm2_swf hp_pole = gm2_swf_exp(gm2_swf_mul(rate->minus_omega, k_feedback_hz));
  const int32_t fb_word = gm2_swf_to_q23(fb_pole);
  const int32_t hp_word = gm2_swf_to_q23(hp_pole);
  const int32_t fb_pass = gm2_swf_to_q23(gm2_swf_sub(gm2_swf_one, fb_pole));
  const int32_t hp_pass = gm2_swf_to_q23(gm2_swf_sub(hp_pole, gm2_swf_one));
  config->coef[GM2_MODDELAY_CORE_FB_FILTER + 0] = config->coef[GM2_MODDELAY_CORE_FB_FILTER + 4] =
      fb_word;
  config->coef[GM2_MODDELAY_CORE_FB_FILTER + 1] = config->coef[GM2_MODDELAY_CORE_FB_FILTER + 5] =
      hp_word;
  config->coef[GM2_MODDELAY_CORE_FB_FILTER + 2] = config->coef[GM2_MODDELAY_CORE_FB_FILTER + 6] =
      fb_pass;
  config->coef[GM2_MODDELAY_CORE_FB_FILTER + 3] = config->coef[GM2_MODDELAY_CORE_FB_FILTER + 7] =
      hp_pass;

  /* P:0x093F60: the input Hi-Cut pole. */
  const gm2_swf hi_pole = gm2_swf_exp(gm2_swf_mul(gm2_swf_hz20(hi_cut), rate->minus_omega));
  const int32_t hi_pass = gm2_swf_to_q23(gm2_swf_sub(gm2_swf_one, hi_pole));
  const int32_t hi_word = gm2_swf_to_q23(hi_pole);
  config->coef[GM2_MODDELAY_CORE_HI_CUT + 0] = config->coef[GM2_MODDELAY_CORE_HI_CUT + 2] = hi_pass;
  config->coef[GM2_MODDELAY_CORE_HI_CUT + 1] = config->coef[GM2_MODDELAY_CORE_HI_CUT + 3] = hi_word;

  config->lfo_set = subtype == GM2_MODDELAY_DESIGN_TRICHORUS ? GM2_MODDELAY_CORE_LFO_TRICHORUS
                    : subtype == GM2_MODDELAY_DESIGN_TRICHORUS_ASYM
                        ? GM2_MODDELAY_CORE_LFO_TRICHORUS_ASYM
                        : GM2_MODDELAY_CORE_LFO_UNIFORM;
  config->note = (unsigned)tempo;
  /* P:0x093FC0 pins the mix word at full on the three subtypes with no Mix knob. */
  const int no_mix = tz_flanger || subtype == GM2_MODDELAY_DESIGN_VIBRATO;
  gm2_mix_words(raw->out_level, no_mix ? 100 : raw->mix, &config->wet, &config->dry);
}
