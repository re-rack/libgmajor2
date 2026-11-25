#include "effect_pitch_words.h"
#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_pitch.h"
#include "gm2_pitch_setup.h"

#define PITCH_KNOBS 11u

/* Delay 1 and Delay 2 reach 350 ms, more than a 4,096-frame segment holds. */
static const gm2_knob k_knobs[PITCH_KNOBS] = {
    {"PIT Voice1", -2400, 2400, 10, 0},
    {"PIT Voice2", -2400, 2400, -15, 0},
    {"PIT Pan1", -50, 50, -50, 0},
    {"PIT Pan2", -50, 50, 50, 0},
    {"PIT Dly1", 0, 350, 20, GM2_EFFECT_HOLD},
    {"PIT Dly2", 0, 350, 10, GM2_EFFECT_HOLD},
    {"PIT FB1", 0, 100, 0, 0},
    {"PIT FB2", 0, 100, 0, 0},
    {"PIT Level1", -100, 0, 0, 0},
    {"PIT Level2", -100, 0, 0, 0},
    {"PIT " GM2_MIX_GLYPHS, 0, 100, 25, 0},
};

static void pitch_raw(const int32_t *values, gm2_pitch_setup_raw *raw) {
  *raw = (gm2_pitch_setup_raw){
      .voice_1 = (int)values[0],
      .voice_2 = (int)values[1],
      .pan_1 = (int)values[2],
      .pan_2 = (int)values[3],
      .delay_1 = (int)values[4],
      .delay_2 = (int)values[5],
      .feedback_1 = (int)values[6],
      .feedback_2 = (int)values[7],
      .level_1 = (int)values[8],
      .level_2 = (int)values[9],
      .mix = (int)values[10],
  };
}

static void pitch_mix(const gm2_pitch_config *config, int32_t *wet, int32_t *dry) {
  *wet = config->wet;
  *dry = config->dry;
}

GM2_EFFECT_DEFINE_MIX(pitch, pitch, k_knobs, pitch_raw, gm2_effect_pitch_words, pitch_mix,
                      GM2_EFFECT_PITCH_WORDS)
