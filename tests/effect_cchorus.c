#include "effect_moddelay_words.h"
#include "gm2.h"
#include "gm2_cchorus.h"
#include "gm2_cchorus_setup.h"
#include "gm2_effect.h"

#define CCHORUS_KNOBS 4u

static const gm2_knob k_knobs[CCHORUS_KNOBS] = {
    {"CHO Speed", 0, 208, 31, 0},
    {"CHO Depth", 0, 100, 50, 0},
    {"CHO Tempo", 0, 16, 0, 0},
    {"CHO HiCut", 0, 60, 58, 0},
};

static void cchorus_raw(const int32_t *values, gm2_cchorus_setup_raw *raw) {
  *raw = (gm2_cchorus_setup_raw){
      .speed = (int)values[0],
      .depth = (int)values[1],
      .tempo = (int)values[2],
      .hi_cut = (int)values[3],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

GM2_EFFECT_DEFINE_LFO(cchorus, cchorus, k_knobs, cchorus_raw, gm2_effect_moddelay_words,
                      GM2_EFFECT_MODDELAY_WORDS)
