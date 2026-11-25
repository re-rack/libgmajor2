/* Dynamic Delay setup, Delay block subtype 1: the twenty coefficient words
 * X+0..19, both tap offsets, the Mix pair X:0x098ED0/X:0x098ED1, the four ducking
 * words X+26..29 and the control tick's period, from P:0x09423F and P:0x09494E.
 */
#include "gm2_dyndelay_setup.h"

#include "gm2_delay_design.h"
#include "gm2_dsp_math.h"
#include "gm2_mix.h"
#include "gm2_swf.h"
#include "gm2_tables.h"
#include "gm2_tick.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  DELAY_MIN = 0,
  DELAY_MAX = 1800,
  OFFSET_MIN = 0,
  OFFSET_MAX = 200,
  SENSE_MIN = -50,
  SENSE_MAX = 0,
  DAMP_MIN = 0,
  DAMP_MAX = 100,
  RELEASE_MIN = 11,
  RELEASE_MAX = 21,
};

static const gm2_swf k_sense_step = GM2_SWF_CONST(0x51EB85u, 0x7Au);  /* 0.02 */
static const gm2_swf k_damp_base = GM2_SWF_CONST(0x500000u, 0x83u);   /* 10 */
static const gm2_swf k_damp_step = GM2_SWF_CONST(0x99999Au, 0x7Bu);   /* -0.05 */
static const gm2_swf k_attack_time = GM2_SWF_CONST(0x500000u, 0x84u); /* 20 ms */

int gm2_dyndelay_setup_config(const gm2_context *context, const gm2_dyndelay_setup_raw *raw,
                              gm2_dyndelay_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }
  const int sense = gm2_clamp_raw(raw->sense, SENSE_MIN, SENSE_MAX);
  const int damp = gm2_clamp_raw(raw->damp, DAMP_MIN, DAMP_MAX);
  const int release = gm2_clamp_raw(raw->release, RELEASE_MIN, RELEASE_MAX);

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  gm2_mix_words(raw->mix, &config->wet, &config->dry);

  gm2_delay_design_matrix(GM2_DELAY_DESIGN_DYNDELAY, raw->feedback, 0,
                          &config->coef[GM2_DELAY_CORE_INPUT]);
  gm2_delay_design_output(GM2_DELAY_DESIGN_DYNDELAY, 0, 0, 0, config->wet,
                          &config->coef[GM2_DELAY_CORE_OUTPUT]);
  gm2_delay_design_filters(&rate_words, GM2_DELAY_DESIGN_DYNDELAY, raw->fb_hi_cut, raw->fb_lo_cut,
                           &config->coef[GM2_DELAY_CORE_FILTER]);

  const int32_t ms =
      gm2_delay_design_ms(raw->delay, DELAY_MIN, DELAY_MAX, raw->tempo, raw->beat_ms);
  const int32_t offset = gm2_delay_design_ms(raw->offset, OFFSET_MIN, OFFSET_MAX, 0, raw->beat_ms);
  config->tap_offset[0] = gm2_delay_design_tap_offset(&rate_words, ms, 0u);
  config->tap_offset[1] = gm2_delay_design_tap_offset(&rate_words, offset, 1u);

  /* P:0x09494E: the ducking words; the attack and release are shares of the gap
   * per tick. */
  const gm2_swf ticks_a_ms = gm2_swf_div(gm2_swf_one, gm2_swf_from_int(GM2_TICK_MS));

  const gm2_swf sense_value = gm2_swf_mac(gm2_swf_one, gm2_swf_from_int(sense), k_sense_step);
  config->duck[GM2_DYNDELAY_DUCK_SENSE] = gm2_swf_to_q23(sense_value);

  config->duck[GM2_DYNDELAY_DUCK_DAMP] =
      gm2_swf_to_q23(gm2_swf_pow(k_damp_base, gm2_swf_mul(gm2_swf_from_int(damp), k_damp_step)));

  config->duck[GM2_DYNDELAY_DUCK_ATTACK] = gm2_swf_to_q23(gm2_swf_sub(
      gm2_swf_one,
      gm2_swf_exp(gm2_swf_div(gm2_swf_minus_one, gm2_swf_mul(k_attack_time, ticks_a_ms)))));

  config->duck[GM2_DYNDELAY_DUCK_RELEASE] = gm2_swf_to_q23(gm2_swf_sub(
      gm2_swf_one, gm2_swf_exp(gm2_swf_div(gm2_swf_minus_one,
                                           gm2_swf_mul(gm2_time_table[release], ticks_a_ms)))));
  config->tick_chunks = gm2_tick_chunks(rate);
  return 0;
}
