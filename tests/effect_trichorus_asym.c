#include "effect_moddelay_words.h"
#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_trichorus_asym.h"
#include "gm2_trichorus_asym_setup.h"

#define TRICHORUS_ASYM_KNOBS 5u

static const gm2_knob k_knobs[TRICHORUS_ASYM_KNOBS] = {
    {"CHO Speed", 0, 208, 31, 0}, {"CHO Depth", 0, 100, 50, 0}, {"CHO Tempo", 0, 16, 0, 0},
    {"CHO Width", 1, 100, 50, 0}, {"CHO PhaRev", 0, 1, 1, 0},
};

static void trichorus_asym_raw(const int32_t *values, gm2_trichorus_asym_setup_raw *raw) {
  *raw = (gm2_trichorus_asym_setup_raw){
      .speed = (int)values[0],
      .depth = (int)values[1],
      .tempo = (int)values[2],
      .width = (int)values[3],
      .phase_reverse = (int)values[4],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

GM2_EFFECT_DEFINE_LFO(trichorus_asym, trichorus_asym, k_knobs, trichorus_asym_raw,
                      gm2_effect_moddelay_words, GM2_EFFECT_MODDELAY_WORDS)
