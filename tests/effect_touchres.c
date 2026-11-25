#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_res_core.h"
#include "gm2_touchres.h"
#include "gm2_touchres_setup.h"

#define TOUCHRES_KNOBS 5u
/* The twenty coefficient words, then the follower's two pairs, the Q and the tail
 * multiplier, then the Mix pair. The Style value and the FrqMax frequency reach no
 * word of their own: the tick reads both from the parameter bank. */
#define TOUCHRES_SETUP_WORDS (GM2_RES_CORE_COEFS + 14u)

static const gm2_knob k_knobs[TOUCHRES_KNOBS] = {
    {"FIL Style", 0, 1, 1, 0},
    {"FIL Sensi", 0, 10, 8, 0},
    {"FIL Resp", 0, 2, 2, 0},
    {"FIL FrqMax", 34, 54, 54, 0},
    {"FIL " GM2_MIX_GLYPHS, 0, 100, 100, 0},
};

static void touchres_raw(const int32_t *values, gm2_touchres_setup_raw *raw) {
  *raw = (gm2_touchres_setup_raw){
      .style = (int)values[0],
      .sense = (int)values[1],
      .response = (int)values[2],
      .freq_max = (int)values[3],
      .mix = (int)values[4],
  };
}

/* Each pair in the firmware's memory order, exponent word first, then the Mix
 * knob's pair at Y:0x3EF/0x3F0. */
static void touchres_words(const gm2_touchres_config *config, int32_t *words) {
  for (unsigned i = 0; i < GM2_RES_CORE_COEFS; i++) {
    words[i] = config->coef[i];
  }
  int32_t *tail = words + GM2_RES_CORE_COEFS;
  tail[0] = config->words.coeff.e;
  tail[1] = config->words.coeff.m;
  tail[2] = config->words.threshold.e;
  tail[3] = config->words.threshold.m;
  tail[4] = config->words.centre.e;
  tail[5] = config->words.centre.m;
  tail[6] = config->words.span.e;
  tail[7] = config->words.span.m;
  tail[8] = config->words.shape.q.e;
  tail[9] = config->words.shape.q.m;
  tail[10] = config->words.shape.scale.e;
  tail[11] = config->words.shape.scale.m;
  words[GM2_RES_CORE_COEFS + 12u] = config->wet;
  words[GM2_RES_CORE_COEFS + 13u] = config->dry;
}

static void touchres_mix(const gm2_touchres_config *config, int32_t *wet, int32_t *dry) {
  *wet = config->wet;
  *dry = config->dry;
}

GM2_EFFECT_DEFINE_MIX(touchres, touchres, k_knobs, touchres_raw, touchres_words, touchres_mix,
                      TOUCHRES_SETUP_WORDS)
