#include "effect_moddelay_words.h"
#include "gm2.h"
#include "gm2_cflanger.h"
#include "gm2_cflanger_setup.h"
#include "gm2_effect.h"

#define CFLANGER_KNOBS 8u

static const gm2_knob k_knobs[CFLANGER_KNOBS] = {
    {"FLA Speed", 0, 208, 24, 0},
    {"FLA Depth", 0, 100, 25, 0},
    {"FLA Tempo", 0, 16, 0, 0},
    {"FLA HiCut", 0, 60, 58, 0},
    {"FLA FB", -100, 100, -80, 0},
    {"FLA FB Cut", 0, 60, 54, 0},
    {"FLA " GM2_MIX_GLYPHS, 0, 100, 50, 0},
    {"FLA OutLev", -100, 0, 0, 0},
};

static void cflanger_raw(const int32_t *values, gm2_cflanger_setup_raw *raw) {
  *raw = (gm2_cflanger_setup_raw){
      .out_level = (int)values[7],
      .speed = (int)values[0],
      .depth = (int)values[1],
      .tempo = (int)values[2],
      .hi_cut = (int)values[3],
      .feedback = (int)values[4],
      .fb_cut = (int)values[5],
      .mix = (int)values[6],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

GM2_EFFECT_DEFINE_LFO_MIX(cflanger, cflanger, k_knobs, cflanger_raw, gm2_effect_moddelay_words,
                          GM2_EFFECT_MODDELAY_WORDS)
