#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_gate.h"
#include "gm2_gate_setup.h"

#define GATE_KNOBS 4u

static const gm2_knob k_knobs[GATE_KNOBS] = {
    {"NG Mode", 0, 1, 0, 0},
    {"NG Thres", -61, 0, -20, 0},
    {"NG Damp", 0, 90, 90, 0},
    {"NG Release", 3, 200, 45, 0},
};

static void gate_raw(const int32_t *values, gm2_gate_setup_raw *raw) {
  *raw = (gm2_gate_setup_raw){
      .mode = (int)values[0],
      .threshold = (int)values[1],
      .damping = (int)values[2],
      .release = (int)values[3],
  };
}

static void gate_words(const gm2_gate_config *config, int32_t *words) {
  for (unsigned i = 0; i < GM2_GATE_COEFS; i++) {
    words[i] = config->coef[i];
  }
}

GM2_EFFECT_DEFINE(gate, gate, k_knobs, gate_raw, gate_words, GM2_GATE_COEFS)
