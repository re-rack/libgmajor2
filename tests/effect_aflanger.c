#include "effect_moddelay_words.h"
#include "gm2.h"
#include "gm2_aflanger.h"
#include "gm2_aflanger_setup.h"
#include "gm2_effect.h"

#define AFLANGER_KNOBS 11u

static const gm2_knob k_knobs[AFLANGER_KNOBS] = {
    {"FLA Speed", 0, 208, 17, 0},  {"FLA Depth", 0, 100, 20, 0},
    {"FLA Tempo", 0, 16, 0, 0},    {"FLA HiCut", 0, 60, 58, 0},
    {"FLA FB", -100, 100, -80, 0}, {"FLA FB Cut", 0, 60, 54, 0},
    {"FLA Delay", 1, 500, 45, 0},  {"FLA Gold", 0, 1, 1, 0},
    {"FLA PhaRev", 0, 1, 1, 0},    {"FLA " GM2_MIX_GLYPHS, 0, 100, 50, 0},
    {"FLA OutLev", -100, 0, 0, 0},
};

static void aflanger_raw(const int32_t *values, gm2_aflanger_setup_raw *raw) {
  *raw = (gm2_aflanger_setup_raw){
      .out_level = (int)values[10],
      .speed = (int)values[0],
      .depth = (int)values[1],
      .tempo = (int)values[2],
      .hi_cut = (int)values[3],
      .feedback = (int)values[4],
      .fb_cut = (int)values[5],
      .delay = (int)values[6],
      .golden_ratio = (int)values[7],
      .phase_reverse = (int)values[8],
      .mix = (int)values[9],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

static void aflanger_mix(const gm2_aflanger_config *config, int32_t *wet, int32_t *dry) {
  *wet = config->wet;
  *dry = config->dry;
}

GM2_EFFECT_DEFINE_LFO_MIX(aflanger, aflanger, k_knobs, aflanger_raw, gm2_effect_moddelay_words,
                          aflanger_mix, GM2_EFFECT_MODDELAY_WORDS)
