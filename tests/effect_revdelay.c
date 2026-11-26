#include "gm2.h"
#include "gm2_delay_core.h"
#include "gm2_effect.h"
#include "gm2_revdelay.h"
#include "gm2_revdelay_alt.h"
#include "gm2_revdelay_setup.h"

#define REVDELAY_KNOBS 7u
/* X+0..19, the grain length Y+26 and the Mix pair; the grain period in chunks
 * follows from the length and the rate, so it is not a word. */
#define REVDELAY_SETUP_WORDS (GM2_DELAY_CORE_COEFS + 1u + 2u)

static const gm2_knob k_knobs[REVDELAY_KNOBS] = {
    {"DLY Dly", 50, 1000, 160, GM2_EFFECT_HOLD},
    {"DLY Tempo", 0, 16, 0, 0},
    {"DLY FB", 0, 100, 30, 0},
    {"DLY FBHcut", 40, 60, 54, 0},
    {"DLY FBLcut", 0, 40, 10, 0},
    {"DLY " GM2_MIX_GLYPHS, 0, 100, 15, 0},
    {"DLY OutLev", -100, 0, 0, 0},
};

static void revdelay_raw(const int32_t *values, gm2_revdelay_setup_raw *raw) {
  *raw = (gm2_revdelay_setup_raw){
      .out_level = (int)values[6],
      .delay = (int)values[0],
      .tempo = (int)values[1],
      .feedback = (int)values[2],
      .fb_hi_cut = (int)values[3],
      .fb_lo_cut = (int)values[4],
      .mix = (int)values[5],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

static void revdelay_words(const gm2_revdelay_config *config, int32_t *words) {
  for (unsigned word = 0; word < GM2_DELAY_CORE_COEFS; word++) {
    words[word] = config->coef[word];
  }
  words[GM2_DELAY_CORE_COEFS] = config->delay_ms;
  words[GM2_DELAY_CORE_COEFS + 1u] = config->wet;
  words[GM2_DELAY_CORE_COEFS + 2u] = config->dry;
}

GM2_EFFECT_DEFINE_MIX(revdelay, revdelay, k_knobs, revdelay_raw, revdelay_words,
                      REVDELAY_SETUP_WORDS)
GM2_EFFECT_DEFINE_MIX(revdelay_alt, revdelay_alt, k_knobs, revdelay_raw, revdelay_words,
                      REVDELAY_SETUP_WORDS)
