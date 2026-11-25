#include "gm2.h"
#include "gm2_autowah.h"
#include "gm2_autowah_setup.h"
#include "gm2_effect.h"

#define AUTOWAH_KNOBS 6u
/* shift, shape, the staged step and the Mix pair; the Tempo value only picks the
 * staged step and has no word of its own. */
#define AUTOWAH_SETUP_WORDS 5u

static const gm2_knob k_knobs[AUTOWAH_KNOBS] = {
    {"FIL Speed", 0, 208, 167, 0},
    {"FIL Tempo", 0, 16, 9, 0},
    {"FIL Shape", 0, 1, 1, 0},
    {"FIL Range", 0, 1, 0, 0},
    {"FIL " GM2_MIX_GLYPHS, 0, 100, 100, 0},
    {"FIL OutLev", -100, 0, 0, 0},
};

static void autowah_raw(const int32_t *values, gm2_autowah_setup_raw *raw) {
  *raw = (gm2_autowah_setup_raw){
      .out_level = (int)values[5],
      .speed = (int)values[0],
      .tempo = (int)values[1],
      .shape = (int)values[2],
      .range = (int)values[3],
      .mix = (int)values[4],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

/* The words the updaters published: Y:0x40A the Range shift, Y:0x414 the LFO
 * shape, X:0x09899F the staged LFO step, then the Mix knob's pair at
 * Y:0x41A/0x41B. */
static void autowah_words(const gm2_autowah_config *config, int32_t *words) {
  words[0] = config->shift;
  words[1] = config->shape;
  words[2] = config->lfo_step;
  words[3] = config->wet;
  words[4] = config->dry;
}

static void autowah_mix(const gm2_autowah_config *config, int32_t *wet, int32_t *dry) {
  *wet = config->wet;
  *dry = config->dry;
}

GM2_EFFECT_DEFINE_LFO_MIX(autowah, autowah, k_knobs, autowah_raw, autowah_words, autowah_mix,
                          AUTOWAH_SETUP_WORDS)
