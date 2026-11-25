#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_vintunivibe.h"
#include "gm2_vintunivibe_setup.h"

#define VINTUNIVIBE_KNOBS 10u
#define VINTUNIVIBE_SETUP_WORDS (GM2_UNIVIBE_CORE_COEFS + 3u)

static const gm2_knob k_knobs[VINTUNIVIBE_KNOBS] = {
    {"UNI Speed1", 37, 184, 128, 0}, {"UNI Speed2", 37, 184, 37, 0},
    {"UNI Depth", 0, 100, 90, 0},    {"UNI Tempo1", 0, 16, 0, 0},
    {"UNI Tempo2", 0, 16, 0, 0},     {"UNI Att", 1, 100, 1, 0},
    {"UNI Rel", 1, 100, 1, 0},       {"UNI Select", 0, 1, 0, 0},
    {"UNI PhaRev", 0, 1, 0, 0},      {"UNI " GM2_MIX_GLYPHS, 0, 100, 50, 0},
};

static void vintunivibe_raw(const int32_t *values, gm2_vintunivibe_setup_raw *raw) {
  *raw = (gm2_vintunivibe_setup_raw){
      .speed_1 = (int)values[0],
      .speed_2 = (int)values[1],
      .depth = (int)values[2],
      .tempo_1 = (int)values[3],
      .tempo_2 = (int)values[4],
      .attack = (int)values[5],
      .release = (int)values[6],
      .select = (int)values[7],
      .phase_reverse = (int)values[8],
      .mix = (int)values[9],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

/* The twenty-one staged words the port holds, the LFO speed's target Y:0x046C
 * and the waveshaper's two weights Y:0x0474/0x0475. */
static void vintunivibe_words(const gm2_vintunivibe_config *config, int32_t *words) {
  for (unsigned i = 0; i < GM2_UNIVIBE_CORE_COEFS; i++) {
    words[i] = config->coef[i];
  }
  words[GM2_UNIVIBE_CORE_COEFS] = config->lfo_speed;
  words[GM2_UNIVIBE_CORE_COEFS + 1u] = config->shaper[0];
  words[GM2_UNIVIBE_CORE_COEFS + 2u] = config->shaper[1];
}

GM2_EFFECT_DEFINE_LFO(vintunivibe, vintunivibe, k_knobs, vintunivibe_raw, vintunivibe_words,
                      VINTUNIVIBE_SETUP_WORDS)
