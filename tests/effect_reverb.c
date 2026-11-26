/* The Reverb's four fixtures. Spring, Hall, Room and Plate (Reverb subtypes 0..3)
 * are one algorithm at four factory settings, the banks P:0x0951AD stages from
 * X:0x09A2E4, X:0x09A2F2, X:0x09A300 and X:0x09A30E: each fixture's knob
 * defaults are one bank, Mix included. */
#include "gm2.h"
#include "gm2_effect.h"
#include "gm2_reverb.h"
#include "gm2_reverb_alt.h"
#include "gm2_reverb_setup.h"

#define REVERB_SETUP_WORDS                                                                         \
  (GM2_REVERB_COEFS + GM2_REVERB_OFFSETS + GM2_REVERB_TANK_SIGNS + 2u * GM2_REVERB_EARLY_TAPS)

#define REVERB_KNOBS(decay, predelay, shape, size, hicol, hifact, locol, lofact, revlev, diffuse,  \
                     mix)                                                                          \
  {                                                                                                \
    {"REV Decay", 1, 200, decay, 0}, {"REV PreDly", 0, 100, predelay, GM2_EFFECT_HOLD},            \
        {"REV Shape", 0, 2, shape, 0}, {"REV Size", 0, 7, size, 0}, {"REV HiCol", 0, 6, hicol, 0}, \
        {"REV HiFact", -25, 25, hifact, 0}, {"REV LoCol", 0, 6, locol, 0},                         \
        {"REV LoFact", -25, 25, lofact, 0}, {"REV RoomLv", -100, 0, 0, 0},                         \
        {"REV RevLev", -100, 0, revlev, 0}, {"REV Diffus", -25, 25, diffuse, 0},                   \
        {"REV " GM2_MIX_GLYPHS, 0, 100, mix, 0}, {                                                 \
      "REV OutLev", -100, 0, 0, 0,                                                                 \
    }                                                                                              \
  }

static const gm2_knob k_spring_knobs[] = REVERB_KNOBS(20, 0, 0, 3, 4, 6, 3, 0, -3, 10, 20);
static const gm2_knob k_hall_knobs[] = REVERB_KNOBS(25, 50, 0, 4, 3, 0, 4, 9, -6, 0, 25);
static const gm2_knob k_room_knobs[] = REVERB_KNOBS(12, 40, 0, 3, 2, 0, 4, 9, -6, 0, 25);
static const gm2_knob k_plate_knobs[] = REVERB_KNOBS(15, 5, 1, 3, 1, 5, 5, 6, -6, 10, 16);

static void reverb_raw(const int32_t *values, gm2_reverb_setup_raw *raw) {
  *raw = (gm2_reverb_setup_raw){
      .decay = (int)values[0],
      .pre_delay = (int)values[1],
      .shape = (int)values[2],
      .size = (int)values[3],
      .hi_color = (int)values[4],
      .hi_factor = (int)values[5],
      .lo_color = (int)values[6],
      .lo_factor = (int)values[7],
      .room_level = (int)values[8],
      .reverb_level = (int)values[9],
      .diffuse = (int)values[10],
      .mix = (int)values[11],
      .out_level = (int)values[12],
  };
}

/* The firmware adapter's order: the eighty staged words, the forty-two offset
 * deltas, the fourteen matrix signs and the two tap sets. */
static void reverb_words(const gm2_reverb_config *config, int32_t *words) {
  unsigned at = 0;
  for (unsigned i = 0; i < GM2_REVERB_COEFS; i++) {
    words[at++] = config->coef[i];
  }
  for (unsigned i = 0; i < GM2_REVERB_OFFSETS; i++) {
    words[at++] = config->offsets[i];
  }
  for (unsigned i = 0; i < GM2_REVERB_TANK_SIGNS; i++) {
    words[at++] = config->tank_signs[i];
  }
  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    for (unsigned i = 0; i < GM2_REVERB_EARLY_TAPS; i++) {
      words[at++] = config->early[channel][i];
    }
  }
}

GM2_EFFECT_DEFINE_MIX(reverb_spring, reverb, k_spring_knobs, reverb_raw, reverb_words,
                      REVERB_SETUP_WORDS)
GM2_EFFECT_DEFINE_MIX(reverb_spring_alt, reverb_alt, k_spring_knobs, reverb_raw, reverb_words,
                      REVERB_SETUP_WORDS)
GM2_EFFECT_DEFINE_MIX(reverb_hall, reverb, k_hall_knobs, reverb_raw, reverb_words,
                      REVERB_SETUP_WORDS)
GM2_EFFECT_DEFINE_MIX(reverb_hall_alt, reverb_alt, k_hall_knobs, reverb_raw, reverb_words,
                      REVERB_SETUP_WORDS)
GM2_EFFECT_DEFINE_MIX(reverb_room, reverb, k_room_knobs, reverb_raw, reverb_words,
                      REVERB_SETUP_WORDS)
GM2_EFFECT_DEFINE_MIX(reverb_room_alt, reverb_alt, k_room_knobs, reverb_raw, reverb_words,
                      REVERB_SETUP_WORDS)
GM2_EFFECT_DEFINE_MIX(reverb_plate, reverb, k_plate_knobs, reverb_raw, reverb_words,
                      REVERB_SETUP_WORDS)
GM2_EFFECT_DEFINE_MIX(reverb_plate_alt, reverb_alt, k_plate_knobs, reverb_raw, reverb_words,
                      REVERB_SETUP_WORDS)
