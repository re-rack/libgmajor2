#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_eq.h"
#include "gm2_eq_setup.h"

static const gm2_knob k_knobs[] = {
    {"EQ1 Freq", 25, 241, 60, 0},  {"EQ1 Gain", -12, 12, 6, 0},  {"EQ1 Width", 3, 16, 10, 0},
    {"EQ2 Freq", 25, 241, 100, 0}, {"EQ2 Gain", -12, 12, -6, 0}, {"EQ2 Width", 3, 16, 6, 0},
    {"EQ3 Freq", 25, 241, 180, 0}, {"EQ3 Gain", -12, 12, 3, 0},  {"EQ3 Width", 3, 16, 4, 0},
};

static void eq_raw(const int32_t *values, gm2_eq_setup_raw *raw) {
  for (unsigned band = 0; band < GM2_EQ_BANDS; band++) {
    raw->band[band].freq = (int)values[3u * band + 0u];
    raw->band[band].gain = (int)values[3u * band + 1u];
    raw->band[band].width = (int)values[3u * band + 2u];
  }
}

static void eq_words(const gm2_eq_config *config, int32_t *words) {
  for (unsigned i = 0; i < GM2_EQ_COEFS; i++) {
    words[i] = config->coef[i];
  }
}

GM2_EFFECT_DEFINE(eq, eq, k_knobs, eq_raw, eq_words, GM2_EQ_COEFS)
