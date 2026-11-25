#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_wahwah.h"
#include "gm2_wahwah_setup.h"

#define WAHWAH_KNOBS 2u
#define WAHWAH_SETUP_WORDS 2u

static const gm2_knob k_knobs[WAHWAH_KNOBS] = {
    {"FIL Sweep", 0, 100, 50, 0},
    {"FIL Range", 0, 1, 0, 0},
};

static void wahwah_raw(const int32_t *values, gm2_wahwah_setup_raw *raw) {
  *raw = (gm2_wahwah_setup_raw){
      .sweep = (int)values[0],
      .range = (int)values[1],
  };
}

/* The words the updaters published: Y:0x40A the Range shift and Y:0x40E the sweep
 * word the cubic reads. */
static void wahwah_words(const gm2_wahwah_config *config, int32_t *words) {
  words[0] = config->shift;
  words[1] = config->sweep;
}

GM2_EFFECT_DEFINE(wahwah, wahwah, k_knobs, wahwah_raw, wahwah_words, WAHWAH_SETUP_WORDS)
