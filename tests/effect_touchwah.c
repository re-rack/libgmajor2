#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_touchwah.h"
#include "gm2_touchwah_setup.h"

#define TOUCHWAH_KNOBS 3u
#define TOUCHWAH_SETUP_WORDS 10u

static const gm2_knob k_knobs[TOUCHWAH_KNOBS] = {
    {"FIL Sensi", 0, 10, 5, 0},
    {"FIL Resp", 0, 2, 2, 0},
    {"FIL Range", 0, 1, 0, 0},
};

static void touchwah_raw(const int32_t *values, gm2_touchwah_setup_raw *raw) {
  *raw = (gm2_touchwah_setup_raw){
      .sense = (int)values[0],
      .response = (int)values[1],
      .range = (int)values[2],
  };
}

/* The words the updaters published: Y:0x40A the Range shift and Y:0x415 the
 * envelope decay, then the follower's: UI+46/47 the share of the gap one tick
 * takes, UI+48/49 the gap it takes it from, UI+52/53 the level that reads as zero
 * and UI+54/55 the sweep word per unit of it. Each pair in the firmware's memory
 * order, exponent word first. */
static void touchwah_words(const gm2_touchwah_config *config, int32_t *words) {
  words[0] = config->shift;
  words[1] = config->decay;
  words[2] = config->follow_coeff.e;
  words[3] = config->follow_coeff.m;
  words[4] = config->follow_threshold.e;
  words[5] = config->follow_threshold.m;
  words[6] = config->centre.e;
  words[7] = config->centre.m;
  words[8] = config->span.e;
  words[9] = config->span.m;
}

GM2_EFFECT_DEFINE(touchwah, touchwah, k_knobs, touchwah_raw, touchwah_words, TOUCHWAH_SETUP_WORDS)
