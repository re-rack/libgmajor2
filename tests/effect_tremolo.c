#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_tremolo.h"
#include "gm2_tremolo_setup.h"

#define TREMOLO_KNOBS 7u
#define TREMOLO_SETUP_WORDS 13u

static const gm2_knob k_knobs[TREMOLO_KNOBS] = {
    {"TRM Speed", 0, 208, 167, 0}, {"TRM Depth", 0, 100, 100, 0}, {"TRM Tempo", 0, 16, 9, 0},
    {"TRM Type", 0, 1, 0, 0},      {"TRM Pwidth", 0, 100, 50, 0}, {"TRM HiCut", 0, 60, 58, 0},
    {"TRM OutLev", -100, 0, 0, 0},
};

static void tremolo_raw(const int32_t *values, gm2_tremolo_setup_raw *raw) {
  *raw = (gm2_tremolo_setup_raw){
      .out_level = (int)values[6],
      .speed = (int)values[0],
      .depth = (int)values[1],
      .tempo = (int)values[2],
      .type = (int)values[3],
      .pulse_width = (int)values[4],
      .hi_cut = (int)values[5],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

/* The firmware adapter's order: pulse, step, the waveform routine's address in
 * Y:0x3B5, lag, rise, fall, depth, the smoother and the two Hi-Cut pairs. */
static void tremolo_words(const gm2_tremolo_config *config, int32_t *words) {
  words[0] = config->coef[GM2_LFO_GAIN_CORE_PULSE];
  words[1] = config->coef[GM2_LFO_GAIN_CORE_STEP];
  words[2] = config->wave == GM2_LFO_GAIN_CORE_WAVE_HARD ? 0x000EAF : 0x000E8C;
  words[3] = config->lag;
  words[4] = config->coef[GM2_LFO_GAIN_CORE_RISE];
  words[5] = config->coef[GM2_LFO_GAIN_CORE_FALL];
  words[6] = config->coef[GM2_LFO_GAIN_CORE_DEPTH];
  words[7] = config->smooth[0];
  words[8] = config->smooth[1];
  for (unsigned i = 0; i < 2u * GM2_CHANNELS; i++) {
    words[9u + i] = config->coef[GM2_LFO_GAIN_CORE_HI_CUT + i];
  }
}

GM2_EFFECT_DEFINE_LFO_WET(tremolo, tremolo, k_knobs, tremolo_raw, tremolo_words,
                          TREMOLO_SETUP_WORDS)
