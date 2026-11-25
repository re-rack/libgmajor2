#include "gm2.h"
#include "gm2_comp.h"
#include "gm2_comp_setup.h"
#include "gm2_effect.h"

#define COMP_KNOBS 5u

static const gm2_knob k_knobs[COMP_KNOBS] = {
    {"COM Thres", -40, 0, -20, 0}, {"COM Ratio", 0, 15, 11, 0}, {"COM Attack", 3, 14, 5, 0},
    {"COM Releas", 13, 23, 15, 0}, {"COM Gain", -6, 6, 0, 0},
};

static void comp_raw(const int32_t *values, gm2_comp_setup_raw *raw) {
  *raw = (gm2_comp_setup_raw){
      .threshold = (int)values[0],
      .ratio = (int)values[1],
      .attack = (int)values[2],
      .release = (int)values[3],
      .gain = (int)values[4],
  };
}

static void comp_words(const gm2_comp_config *config, int32_t *words) {
  for (unsigned i = 0; i < GM2_COMP_COEFS; i++) {
    words[i] = config->coef[i];
  }
}

GM2_EFFECT_DEFINE(comp, comp, k_knobs, comp_raw, comp_words, GM2_COMP_COEFS)
