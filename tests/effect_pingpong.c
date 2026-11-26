#include "gm2.h"
#include "gm2_delay_core.h"
#include "gm2_effect.h"
#include "gm2_pingpong.h"
#include "gm2_pingpong_setup.h"

#define PINGPONG_KNOBS 8u
/* The twenty coefficient words, the two tap offsets, then the Mix pair. */
#define PINGPONG_SETUP_WORDS (GM2_DELAY_CORE_COEFS + 2u + 2u)

static const gm2_knob k_knobs[PINGPONG_KNOBS] = {
    {"DLY Dly", 0, 1800, 375, GM2_EFFECT_HOLD},
    {"DLY Tempo", 0, 16, 8, 0},
    {"DLY Width", 0, 100, 75, 0},
    {"DLY FB", 0, 100, 45, 0},
    {"DLY FBHcut", 40, 60, 54, 0},
    {"DLY FBLcut", 0, 40, 10, 0},
    {"DLY " GM2_MIX_GLYPHS, 0, 100, 15, 0},
    {"DLY OutLev", -100, 0, 0, 0},
};

static void pingpong_raw(const int32_t *values, gm2_pingpong_setup_raw *raw) {
  *raw = (gm2_pingpong_setup_raw){
      .out_level = (int)values[7],
      .delay = (int)values[0],
      .tempo = (int)values[1],
      .width = (int)values[2],
      .feedback = (int)values[3],
      .fb_hi_cut = (int)values[4],
      .fb_lo_cut = (int)values[5],
      .mix = (int)values[6],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

static void pingpong_words(const gm2_pingpong_config *config, int32_t *words) {
  for (unsigned word = 0; word < GM2_DELAY_CORE_COEFS; word++) {
    words[word] = config->coef[word];
  }
  words[GM2_DELAY_CORE_COEFS] = config->tap_offset[0];
  words[GM2_DELAY_CORE_COEFS + 1u] = config->tap_offset[1];
  words[GM2_DELAY_CORE_COEFS + 2u] = config->wet;
  words[GM2_DELAY_CORE_COEFS + 3u] = config->dry;
}

GM2_EFFECT_DEFINE_MIX(pingpong, pingpong, k_knobs, pingpong_raw, pingpong_words,
                      PINGPONG_SETUP_WORDS)
