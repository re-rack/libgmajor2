#include "effect_pitch_words.h"
#include "gm2.h"
#include "gm2_detune.h"
#include "gm2_detune_setup.h"
#include "gm2_effect.h"

#define DETUNE_KNOBS 4u

static const gm2_knob k_knobs[DETUNE_KNOBS] = {
    {"PIT VoiceL", -100, 100, 9, 0},
    {"PIT VoiceR", -100, 100, -9, 0},
    {"PIT DlyL", 0, 50, 10, 0},
    {"PIT DlyR", 0, 50, 12, 0},
};

static void detune_raw(const int32_t *values, gm2_detune_setup_raw *raw) {
  *raw = (gm2_detune_setup_raw){
      .voice_l = (int)values[0],
      .voice_r = (int)values[1],
      .delay_l = (int)values[2],
      .delay_r = (int)values[3],
  };
}

GM2_EFFECT_DEFINE(detune, detune, k_knobs, detune_raw, gm2_effect_pitch_words,
                  GM2_EFFECT_PITCH_WORDS)
