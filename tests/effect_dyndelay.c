#include "gm2.h"
#include "gm2_delay_core.h"
#include "gm2_dyndelay.h"
#include "gm2_dyndelay_setup.h"
#include "gm2_effect.h"

#define DYNDELAY_KNOBS 11u
/* X+8 and X+9 are the ducking tick's gain words, not the setup's; the ducking words
 * X+26..29, the two tap offsets and the Mix pair follow. */
#define DYNDELAY_SETUP_WORDS (GM2_DELAY_CORE_COEFS - 2u + GM2_DYNDELAY_DUCK_WORDS + 2u + 2u)

static const gm2_knob k_knobs[DYNDELAY_KNOBS] = {
    {"DLY Dly", 0, 1800, 300, GM2_EFFECT_HOLD},
    {"DLY Tempo", 0, 16, 0, 0},
    {"DLY FB", 0, 100, 35, 0},
    {"DLY FBHcut", 40, 60, 55, 0},
    {"DLY FBLcut", 0, 40, 8, 0},
    {"DLY Offset", 0, 200, 25, GM2_EFFECT_HOLD},
    {"DLY Sense", -50, 0, -25, 0},
    {"DLY Damp", 0, 100, 18, 0},
    {"DLY Rel", 11, 21, 14, 0},
    {"DLY " GM2_MIX_GLYPHS, 0, 100, 15, 0},
    {"DLY OutLev", -100, 0, 0, 0},
};

static void dyndelay_raw(const int32_t *values, gm2_dyndelay_setup_raw *raw) {
  *raw = (gm2_dyndelay_setup_raw){
      .out_level = (int)values[10],
      .delay = (int)values[0],
      .tempo = (int)values[1],
      .feedback = (int)values[2],
      .fb_hi_cut = (int)values[3],
      .fb_lo_cut = (int)values[4],
      .offset = (int)values[5],
      .sense = (int)values[6],
      .damp = (int)values[7],
      .release = (int)values[8],
      .mix = (int)values[9],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

static void dyndelay_mix(const gm2_dyndelay_config *config, int32_t *wet, int32_t *dry) {
  *wet = config->wet;
  *dry = config->dry;
}

static void dyndelay_words(const gm2_dyndelay_config *config, int32_t *words) {
  unsigned word = 0;
  for (unsigned i = 0; i < GM2_DELAY_CORE_COEFS; i++) {
    if (i != GM2_DELAY_CORE_OUTPUT && i != GM2_DELAY_CORE_OUTPUT + 1u) {
      words[word++] = config->coef[i];
    }
  }
  for (unsigned i = 0; i < GM2_DYNDELAY_DUCK_WORDS; i++) {
    words[word++] = config->duck[i];
  }
  words[word++] = config->tap_offset[0];
  words[word++] = config->tap_offset[1];
  words[word++] = config->wet;
  words[word++] = config->dry;
}

GM2_EFFECT_DEFINE_MIX(dyndelay, dyndelay, k_knobs, dyndelay_raw, dyndelay_words, dyndelay_mix,
                      DYNDELAY_SETUP_WORDS)
