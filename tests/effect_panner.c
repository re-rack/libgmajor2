#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_panner.h"
#include "gm2_panner_setup.h"

#define PANNER_KNOBS 4u
#define PANNER_SETUP_WORDS 10u

static const gm2_knob k_knobs[PANNER_KNOBS] = {
    {"PAN Speed", 0, 208, 116, 0},
    {"PAN Width", 0, 100, 100, 0},
    {"PAN Tempo", 0, 16, 0, 0},
    {"PAN OutLev", -100, 0, 0, 0},
};

static void panner_raw(const int32_t *values, gm2_panner_setup_raw *raw) {
  *raw = (gm2_panner_setup_raw){
      .out_level = (int)values[3],
      .speed = (int)values[0],
      .width = (int)values[1],
      .tempo = (int)values[2],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

/* The firmware adapter's order: step, the sine routine's address in Y:0x3B5,
 * lag, depth, the smoother and the two Hi-Cut pairs. The pulse words the sine
 * never reads are not setup words. */
static void panner_words(const gm2_panner_config *config, int32_t *words) {
  words[0] = config->coef[GM2_LFO_GAIN_CORE_STEP];
  words[1] = 0x000EC1;
  words[2] = config->lag;
  words[3] = config->coef[GM2_LFO_GAIN_CORE_DEPTH];
  words[4] = config->smooth[0];
  words[5] = config->smooth[1];
  for (unsigned i = 0; i < 2u * GM2_CHANNELS; i++) {
    words[6u + i] = config->coef[GM2_LFO_GAIN_CORE_HI_CUT + i];
  }
}

GM2_EFFECT_DEFINE_LFO_WET(panner, panner, k_knobs, panner_raw, panner_words, PANNER_SETUP_WORDS)
