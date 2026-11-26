#include "effect_pitch_words.h"
#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_whammy.h"
#include "gm2_whammy_setup.h"

#define WHAMMY_KNOBS 4u

static const gm2_knob k_knobs[WHAMMY_KNOBS] = {
    {"PIT Pitch", 0, 100, 0, 0},
    {"PIT Dir", 0, 1, 1, 0},
    {"PIT Range", 1, 2, 2, 0},
    {"PIT Outlev", -100, 0, 0, 0},
};

static void whammy_raw(const int32_t *values, gm2_whammy_setup_raw *raw) {
  *raw = (gm2_whammy_setup_raw){
      .out_level = (int)values[3],
      .pitch = (int)values[0],
      .direction = (int)values[1],
      .range = (int)values[2],
  };
}

GM2_EFFECT_DEFINE_MIX(whammy, whammy, k_knobs, whammy_raw, gm2_effect_pitch_words,
                      GM2_EFFECT_PITCH_WORDS)
