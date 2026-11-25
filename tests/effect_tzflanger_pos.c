#include "effect_moddelay_words.h"
#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_tzflanger_pos.h"
#include "gm2_tzflanger_pos_setup.h"

#define TZFLANGER_POS_KNOBS 5u

static const gm2_knob k_knobs[TZFLANGER_POS_KNOBS] = {
    {"FLA Speed", 0, 208, 17, 0},  {"FLA Depth", 0, 100, 20, 0}, {"FLA Tempo", 0, 16, 0, 0},
    {"FLA FB", -100, 100, -80, 0}, {"FLA FB Cut", 0, 60, 45, 0},
};

static void tzflanger_pos_raw(const int32_t *values, gm2_tzflanger_pos_setup_raw *raw) {
  *raw = (gm2_tzflanger_pos_setup_raw){
      .speed = (int)values[0],
      .depth = (int)values[1],
      .tempo = (int)values[2],
      .feedback = (int)values[3],
      .fb_cut = (int)values[4],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

GM2_EFFECT_DEFINE_LFO(tzflanger_pos, tzflanger_pos, k_knobs, tzflanger_pos_raw,
                      gm2_effect_moddelay_words, GM2_EFFECT_MODDELAY_WORDS)
