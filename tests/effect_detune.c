#include "effect_pitch_words.h"
#include "gm2.h"
#include "gm2_detune.h"
#include "gm2_detune_setup.h"
#include "gm2_effect.h"

#define DETUNE_KNOBS 5u

static const gm2_knob k_knobs[DETUNE_KNOBS] = {
    {"PIT VoiceL", -100, 100, 9, 0},
    {"PIT VoiceR", -100, 100, -9, 0},
    {"PIT DlyL", 0, 50, 10, 0},
    {"PIT DlyR", 0, 50, 12, 0},
    {"PIT " GM2_MIX_GLYPHS, 0, 100, 25, 0},
};

static void detune_raw(const int32_t *values, gm2_detune_setup_raw *raw) {
  *raw = (gm2_detune_setup_raw){
      .voice_l = (int)values[0],
      .voice_r = (int)values[1],
      .delay_l = (int)values[2],
      .delay_r = (int)values[3],
      .mix = (int)values[4],
  };
}

static void detune_mix(const gm2_detune_config *config, int32_t *wet, int32_t *dry) {
  *wet = config->wet;
  *dry = config->dry;
}

GM2_EFFECT_DEFINE_MIX(detune, detune, k_knobs, detune_raw, gm2_effect_pitch_words, detune_mix,
                      GM2_EFFECT_PITCH_WORDS)
