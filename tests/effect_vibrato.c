#include "effect_moddelay_words.h"
#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_vibrato.h"
#include "gm2_vibrato_setup.h"

#define VIBRATO_KNOBS 5u

static const gm2_knob k_knobs[VIBRATO_KNOBS] = {
    {"VIB Speed", 0, 208, 152, 0}, {"VIB Depth", 0, 100, 7, 0},   {"VIB Tempo", 0, 16, 0, 0},
    {"VIB HiCut", 0, 60, 58, 0},   {"VIB OutLev", -100, 0, 0, 0},
};

static void vibrato_raw(const int32_t *values, gm2_vibrato_setup_raw *raw) {
  *raw = (gm2_vibrato_setup_raw){
      .out_level = (int)values[4],
      .speed = (int)values[0],
      .depth = (int)values[1],
      .tempo = (int)values[2],
      .hi_cut = (int)values[3],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

GM2_EFFECT_DEFINE_LFO_MIX(vibrato, vibrato, k_knobs, vibrato_raw, gm2_effect_moddelay_words,
                          GM2_EFFECT_MODDELAY_WORDS)
