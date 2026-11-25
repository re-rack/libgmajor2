/* The Modulation block's setup words, as its nine chorus, flanger and vibrato
 * subtypes stage them: the thirty words X:0x098BD1..0x098BEE, then the six voice
 * depths Y:0x04BA + 5v, the six staged voice offsets X:0x098C08 + 11v and the Mix
 * knob's wet and dry words X:0x098D1A/X:0x098D1B.
 */
#ifndef GM2_EFFECT_MODDELAY_WORDS_H
#define GM2_EFFECT_MODDELAY_WORDS_H

#include "gm2_moddelay_core.h"

#define GM2_EFFECT_MODDELAY_WORDS (GM2_MODDELAY_CORE_COEFS + 2u * GM2_MODDELAY_CORE_VOICES + 2u)

static inline void gm2_effect_moddelay_words(const gm2_moddelay_core_config *config,
                                             int32_t *words) {
  for (unsigned i = 0; i < GM2_MODDELAY_CORE_COEFS; i++) {
    words[i] = config->coef[i];
  }
  for (unsigned voice = 0; voice < GM2_MODDELAY_CORE_VOICES; voice++) {
    words[GM2_MODDELAY_CORE_COEFS + voice] = config->depth[voice];
    words[GM2_MODDELAY_CORE_COEFS + GM2_MODDELAY_CORE_VOICES + voice] = config->tap_offset[voice];
  }
  words[GM2_MODDELAY_CORE_COEFS + 2u * GM2_MODDELAY_CORE_VOICES] = config->wet;
  words[GM2_MODDELAY_CORE_COEFS + 2u * GM2_MODDELAY_CORE_VOICES + 1u] = config->dry;
}

#endif /* GM2_EFFECT_MODDELAY_WORDS_H */
