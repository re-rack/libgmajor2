#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_wahwah.h"
#include "gm2_wahwah_setup.h"

#define WAHWAH_KNOBS 4u
#define WAHWAH_SETUP_WORDS 4u

static const gm2_knob k_knobs[WAHWAH_KNOBS] = {
    {"FIL Sweep", 0, 100, 50, 0},
    {"FIL Range", 0, 1, 0, 0},
    {"FIL " GM2_MIX_GLYPHS, 0, 100, 100, 0},
    {"FIL OutLev", -100, 0, 0, 0},
};

static void wahwah_raw(const int32_t *values, gm2_wahwah_setup_raw *raw) {
  *raw = (gm2_wahwah_setup_raw){
      .out_level = (int)values[3],
      .sweep = (int)values[0],
      .range = (int)values[1],
      .mix = (int)values[2],
  };
}

/* The words the updaters published: Y:0x40A the Range shift, Y:0x40E the sweep
 * word the cubic reads, then the Mix knob's pair at Y:0x41A/0x41B. */
static void wahwah_words(const gm2_wahwah_config *config, int32_t *words) {
  words[0] = config->shift;
  words[1] = config->sweep;
  words[2] = config->wet;
  words[3] = config->dry;
}

GM2_EFFECT_DEFINE_MIX(wahwah, wahwah, k_knobs, wahwah_raw, wahwah_words, WAHWAH_SETUP_WORDS)
