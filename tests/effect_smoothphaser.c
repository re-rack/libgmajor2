#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_smoothphaser.h"
#include "gm2_smoothphaser_setup.h"

#define SMOOTHPHASER_KNOBS 6u

static const gm2_knob k_knobs[SMOOTHPHASER_KNOBS] = {
    {"PHA Speed", 0, 208, 38, 0},  {"PHA Depth", 0, 100, 80, 0}, {"PHA Tempo", 0, 16, 0, 0},
    {"PHA FB", -100, 100, -90, 0}, {"PHA Range", 0, 1, 0, 0},    {"PHA PhaRev", 0, 1, 1, 0},
};

static void smoothphaser_raw(const int32_t *values, gm2_smoothphaser_setup_raw *raw) {
  *raw = (gm2_smoothphaser_setup_raw){
      .speed = (int)values[0],
      .depth = (int)values[1],
      .tempo = (int)values[2],
      .feedback = (int)values[3],
      .range = (int)values[4],
      .phase_reverse = (int)values[5],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

static void smoothphaser_words(const gm2_smoothphaser_config *config, int32_t *words) {
  for (unsigned i = 0; i < GM2_PHASER_CORE_COEFS; i++) {
    words[i] = config->coef[i];
  }
}

GM2_EFFECT_DEFINE_LFO(smoothphaser, smoothphaser, k_knobs, smoothphaser_raw, smoothphaser_words,
                      GM2_PHASER_CORE_COEFS)
