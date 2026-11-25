#include "gm2.h"
#include "gm2_autowah.h"
#include "gm2_autowah_setup.h"
#include "gm2_effect.h"

#define AUTOWAH_KNOBS 4u
/* shift, shape and the staged step; the Tempo value only picks the staged step
 * and has no word of its own. */
#define AUTOWAH_SETUP_WORDS 3u

static const gm2_knob k_knobs[AUTOWAH_KNOBS] = {
    {"FIL Speed", 0, 208, 167, 0},
    {"FIL Tempo", 0, 16, 9, 0},
    {"FIL Shape", 0, 1, 1, 0},
    {"FIL Range", 0, 1, 0, 0},
};

static void autowah_raw(const int32_t *values, gm2_autowah_setup_raw *raw) {
  *raw = (gm2_autowah_setup_raw){
      .speed = (int)values[0],
      .tempo = (int)values[1],
      .shape = (int)values[2],
      .range = (int)values[3],
      .beat_ms = (double)GM2_EFFECT_BEAT_MS,
  };
}

/* The words the updaters published: Y:0x40A the Range shift, Y:0x414 the LFO
 * shape and X:0x09899F the staged LFO step. */
static void autowah_words(const gm2_autowah_config *config, int32_t *words) {
  words[0] = config->shift;
  words[1] = config->shape;
  words[2] = config->lfo_step;
}

GM2_EFFECT_DEFINE_LFO(autowah, autowah, k_knobs, autowah_raw, autowah_words, AUTOWAH_SETUP_WORDS)
