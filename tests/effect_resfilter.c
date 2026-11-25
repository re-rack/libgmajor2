#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_res_core.h"
#include "gm2_resfilter.h"
#include "gm2_resfilter_setup.h"

#define RESFILTER_KNOBS 3u

static const gm2_knob k_knobs[RESFILTER_KNOBS] = {
    {"FIL Style", 0, 1, 1, 0},
    {"FIL HiCut", 18, 57, 37, 0},
    {"FIL Res", 0, 100, 100, 0},
};

static void resfilter_raw(const int32_t *values, gm2_resfilter_setup_raw *raw) {
  *raw = (gm2_resfilter_setup_raw){
      .style = (int)values[0],
      .hi_cut = (int)values[1],
      .resonance = (int)values[2],
  };
}

static void resfilter_words(const gm2_resfilter_config *config, int32_t *words) {
  for (unsigned i = 0; i < GM2_RES_CORE_COEFS; i++) {
    words[i] = config->coef[i];
  }
}

GM2_EFFECT_DEFINE(resfilter, resfilter, k_knobs, resfilter_raw, resfilter_words, GM2_RES_CORE_COEFS)
