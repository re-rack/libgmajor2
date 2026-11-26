#include "effect_moddelay_words.h"
#include "gm2.h"
#include "gm2_achorus.h"
#include "gm2_achorus_setup.h"
#include "gm2_effect.h"

#define ACHORUS_KNOBS 9u

static const gm2_knob k_knobs[ACHORUS_KNOBS] = {
    {"CHO Speed", 0, 208, 31, 0},  {"CHO Depth", 0, 100, 50, 0},
    {"CHO Tempo", 0, 16, 0, 0},    {"CHO HiCut", 0, 60, 58, 0},
    {"CHO Delay", 1, 500, 140, 0}, {"CHO Gold", 0, 1, 1, 0},
    {"CHO PhaRev", 0, 1, 1, 0},    {"CHO " GM2_MIX_GLYPHS, 0, 100, 50, 0},
    {"CHO OutLev", -100, 0, 0, 0},
};

static void achorus_raw(const int32_t *values, gm2_achorus_setup_raw *raw) {
  *raw = (gm2_achorus_setup_raw){
      .out_level = (int)values[8],
      .speed = (int)values[0],
      .depth = (int)values[1],
      .tempo = (int)values[2],
      .hi_cut = (int)values[3],
      .delay = (int)values[4],
      .golden_ratio = (int)values[5],
      .phase_reverse = (int)values[6],
      .mix = (int)values[7],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

GM2_EFFECT_DEFINE_LFO_MIX(achorus, achorus, k_knobs, achorus_raw, gm2_effect_moddelay_words,
                          GM2_EFFECT_MODDELAY_WORDS)
