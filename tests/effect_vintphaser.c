#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_vintphaser.h"
#include "gm2_vintphaser_setup.h"

#define VINTPHASER_KNOBS 8u

static const gm2_knob k_knobs[VINTPHASER_KNOBS] = {
    {"PHA Speed", 0, 208, 56, 0},
    {"PHA Depth", 0, 100, 90, 0},
    {"PHA Tempo", 0, 16, 0, 0},
    {"PHA FB", -100, 100, -90, 0},
    {"PHA Range", 0, 1, 0, 0},
    {"PHA PhaRev", 0, 1, 0, 0},
    {"PHA " GM2_MIX_GLYPHS, 0, 100, 50, 0},
    {"PHA OutLev", -100, 0, 0, 0},
};

static void vintphaser_raw(const int32_t *values, gm2_vintphaser_setup_raw *raw) {
  *raw = (gm2_vintphaser_setup_raw){
      .out_level = (int)values[7],
      .speed = (int)values[0],
      .depth = (int)values[1],
      .tempo = (int)values[2],
      .feedback = (int)values[3],
      .range = (int)values[4],
      .phase_reverse = (int)values[5],
      .mix = (int)values[6],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

static void vintphaser_words(const gm2_vintphaser_config *config, int32_t *words) {
  for (unsigned i = 0; i < GM2_PHASER_CORE_COEFS; i++) {
    words[i] = config->coef[i];
  }
}

static void vintphaser_mix(const gm2_vintphaser_config *config, int32_t *wet, int32_t *dry) {
  *wet = config->wet;
  *dry = config->dry;
}

GM2_EFFECT_DEFINE_LFO_MIX(vintphaser, vintphaser, k_knobs, vintphaser_raw, vintphaser_words,
                          vintphaser_mix, GM2_PHASER_CORE_COEFS)
