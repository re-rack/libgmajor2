#include "gm2.h"
#include "gm2_delay_core.h"
#include "gm2_dualdelay.h"
#include "gm2_dualdelay_setup.h"
#include "gm2_effect.h"

#define DUALDELAY_KNOBS 12u
/* The twenty coefficient words, the two tap offsets, then the Mix pair. */
#define DUALDELAY_SETUP_WORDS (GM2_DELAY_CORE_COEFS + 2u + 2u)

static const gm2_knob k_knobs[DUALDELAY_KNOBS] = {
    {"DLY Dly1", 0, 1800, 280, GM2_EFFECT_HOLD},
    {"DLY Dly2", 0, 1800, 420, GM2_EFFECT_HOLD},
    {"DLY Tempo1", 0, 16, 0, 0},
    {"DLY Tempo2", 0, 16, 9, 0},
    {"DLY FB1", 0, 100, 35, 0},
    {"DLY FB2", 0, 100, 45, 0},
    {"DLY FBHcut", 40, 60, 53, 0},
    {"DLY FBLcut", 0, 40, 12, 0},
    {"DLY Pan1", -50, 50, -30, 0},
    {"DLY Pan2", -50, 50, 35, 0},
    {"DLY " GM2_MIX_GLYPHS, 0, 100, 15, 0},
    {"DLY OutLev", -100, 0, 0, 0},
};

static void dualdelay_raw(const int32_t *values, gm2_dualdelay_setup_raw *raw) {
  *raw = (gm2_dualdelay_setup_raw){
      .out_level = (int)values[11],
      .delay_1 = (int)values[0],
      .delay_2 = (int)values[1],
      .tempo_1 = (int)values[2],
      .tempo_2 = (int)values[3],
      .feedback_1 = (int)values[4],
      .feedback_2 = (int)values[5],
      .fb_hi_cut = (int)values[6],
      .fb_lo_cut = (int)values[7],
      .pan_1 = (int)values[8],
      .pan_2 = (int)values[9],
      .mix = (int)values[10],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

static void dualdelay_mix(const gm2_dualdelay_config *config, int32_t *wet, int32_t *dry) {
  *wet = config->wet;
  *dry = config->dry;
}

static void dualdelay_words(const gm2_dualdelay_config *config, int32_t *words) {
  for (unsigned word = 0; word < GM2_DELAY_CORE_COEFS; word++) {
    words[word] = config->coef[word];
  }
  words[GM2_DELAY_CORE_COEFS] = config->tap_offset[0];
  words[GM2_DELAY_CORE_COEFS + 1u] = config->tap_offset[1];
  words[GM2_DELAY_CORE_COEFS + 2u] = config->wet;
  words[GM2_DELAY_CORE_COEFS + 3u] = config->dry;
}

GM2_EFFECT_DEFINE_MIX(dualdelay, dualdelay, k_knobs, dualdelay_raw, dualdelay_words, dualdelay_mix,
                      DUALDELAY_SETUP_WORDS)
