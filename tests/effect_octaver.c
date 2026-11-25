#include "effect_pitch_words.h"
#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_octaver.h"
#include "gm2_octaver_setup.h"

#define OCTAVER_KNOBS 2u

static const gm2_knob k_knobs[OCTAVER_KNOBS] = {
    {"PIT Dir", 0, 1, 0, 0},
    {"PIT Range", 1, 2, 1, 0},
};

static void octaver_raw(const int32_t *values, gm2_octaver_setup_raw *raw) {
  *raw = (gm2_octaver_setup_raw){
      .direction = (int)values[0],
      .range = (int)values[1],
  };
}

GM2_EFFECT_DEFINE(octaver, octaver, k_knobs, octaver_raw, gm2_effect_pitch_words,
                  GM2_EFFECT_PITCH_WORDS)
