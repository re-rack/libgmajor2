#include "effect_pitch_words.h"
#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_ipitch.h"
#include "gm2_ipitch_alt.h"
#include "gm2_ipitch_setup.h"

#define IPITCH_KNOBS 12u

/* Voice 2's default -15 lies outside the descriptor's -12..12, which the
 * accessor clamps to -12. */
static const gm2_knob k_knobs[IPITCH_KNOBS] = {
    {"PIT Voice1", -12, 12, 10, 0},
    {"PIT Voice2", -12, 12, -12, 0},
    {"PIT Pan1", -50, 50, -50, 0},
    {"PIT Pan2", -50, 50, 50, 0},
    {"PIT Dly1", 0, 350, 20, GM2_EFFECT_HOLD},
    {"PIT Dly2", 0, 350, 10, GM2_EFFECT_HOLD},
    {"Key", 0, 11, 0, 0},
    {"Scale", 0, 17, 0, 0},
    {"PIT Level1", -100, 0, 0, 0},
    {"PIT Level2", -100, 0, 0, 0},
    {"PIT " GM2_MIX_GLYPHS, 0, 100, 25, 0},
    {"PIT Outlev", -100, 0, 0, 0},
};

static void ipitch_raw(const int32_t *values, gm2_ipitch_setup_raw *raw) {
  *raw = (gm2_ipitch_setup_raw){
      .out_level = (int)values[11],
      .voice_1 = (int)values[0],
      .voice_2 = (int)values[1],
      .pan_1 = (int)values[2],
      .pan_2 = (int)values[3],
      .delay_1 = (int)values[4],
      .delay_2 = (int)values[5],
      .key = (int)values[6],
      .scale = (int)values[7],
      .level_1 = (int)values[8],
      .level_2 = (int)values[9],
      .mix = (int)values[10],
  };
}

static void ipitch_mix(const gm2_ipitch_config *config, int32_t *wet, int32_t *dry) {
  *wet = config->wet;
  *dry = config->dry;
}

GM2_EFFECT_DEFINE_MIX(ipitch, ipitch, k_knobs, ipitch_raw, gm2_effect_pitch_words, ipitch_mix,
                      GM2_EFFECT_PITCH_WORDS)
GM2_EFFECT_DEFINE_MIX(ipitch_alt, ipitch_alt, k_knobs, ipitch_raw, gm2_effect_pitch_words,
                      ipitch_mix, GM2_EFFECT_PITCH_WORDS)
