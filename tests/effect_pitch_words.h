/* The Pitch block's setup words, as the five subtypes stage them: the feedbacks
 * and pan gains X:0x0987C6..X:0x0987CB, the delays X:0x0412/X:0x0414, the ratios
 * X:0x041D/X:0x041E, each curve's gain Y:0x0228/Y:0x022A, the curves Y:0x022B and
 * Y:0x0250, the rate factor, the mode word and the reference offset
 * X:0x0416..X:0x0424, and the period window X:0x0566..X:0x0568.
 */
#ifndef GM2_EFFECT_PITCH_WORDS_H
#define GM2_EFFECT_PITCH_WORDS_H

#include "gm2_pitch_core.h"

#define GM2_EFFECT_PITCH_WORDS 92u

/* X:0x0423 once P:0x0910BD has run, on the Intelligent Pitch Shifter. */
#define GM2_EFFECT_PITCH_HARMONIZE 3

static inline void gm2_effect_pitch_words(const gm2_pitch_core_config *config, int32_t *words) {
  unsigned at = 0;
  for (unsigned i = 0; i < GM2_PITCH_CORE_VOICES; i++) {
    words[at++] = config->feedback[i];
  }
  for (unsigned i = 0; i < GM2_PITCH_CORE_VOICES; i++) {
    words[at++] = config->delay[i];
  }
  for (unsigned i = 0; i < GM2_PITCH_CORE_VOICES; i++) {
    words[at++] = config->ratio[i];
  }
  for (unsigned i = 0; i < 2u * GM2_PITCH_CORE_VOICES; i++) {
    words[at++] = config->pan[i];
  }
  for (unsigned voice = 0; voice < GM2_PITCH_CORE_VOICES; voice++) {
    words[at++] = config->curve_gain[voice];
  }
  for (unsigned voice = 0; voice < GM2_PITCH_CORE_VOICES; voice++) {
    for (unsigned i = 0; i < GM2_PITCH_CORE_CURVE_WORDS; i++) {
      words[at++] = config->curve[voice][i];
    }
  }
  words[at++] = config->rate_factor;
  words[at++] = config->harmonize != 0 ? GM2_EFFECT_PITCH_HARMONIZE : 0;
  words[at++] = config->ref_offset;
  words[at++] = config->min_period;
  words[at++] = config->max_period;
  words[at++] = config->lag_count;
}

#endif /* GM2_EFFECT_PITCH_WORDS_H */
