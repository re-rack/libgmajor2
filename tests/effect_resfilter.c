#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_res_core.h"
#include "gm2_resfilter.h"
#include "gm2_resfilter_alt.h"
#include "gm2_resfilter_setup.h"

#define RESFILTER_KNOBS 5u
#define RESFILTER_SETUP_WORDS (GM2_RES_CORE_COEFS + 2u)

static const gm2_knob k_knobs[RESFILTER_KNOBS] = {
    {"FIL Style", 0, 1, 1, 0},     {"FIL HiCut", 18, 57, 37, 0},
    {"FIL Res", 0, 100, 100, 0},   {"FIL " GM2_MIX_GLYPHS, 0, 100, 100, 0},
    {"FIL OutLev", -100, 0, 0, 0},
};

static void resfilter_raw(const int32_t *values, gm2_resfilter_setup_raw *raw) {
  *raw = (gm2_resfilter_setup_raw){
      .out_level = (int)values[4],
      .style = (int)values[0],
      .hi_cut = (int)values[1],
      .resonance = (int)values[2],
      .mix = (int)values[3],
  };
}

/* The twenty coefficient words, then the Mix knob's pair at Y:0x3EF/0x3F0. */
static void resfilter_words(const gm2_resfilter_config *config, int32_t *words) {
  for (unsigned i = 0; i < GM2_RES_CORE_COEFS; i++) {
    words[i] = config->coef[i];
  }
  words[GM2_RES_CORE_COEFS] = config->wet;
  words[GM2_RES_CORE_COEFS + 1u] = config->dry;
}

GM2_EFFECT_DEFINE_MIX(resfilter, resfilter, k_knobs, resfilter_raw, resfilter_words,
                      RESFILTER_SETUP_WORDS)
GM2_EFFECT_DEFINE_MIX(resfilter_alt, resfilter_alt, k_knobs, resfilter_raw, resfilter_words,
                      RESFILTER_SETUP_WORDS)
