/* One effect described once: its knob table and the native adapter the checker
 * drives. The recorder reads the same table through the firmware, so the two
 * sides cannot drift.
 */
#ifndef GM2_EFFECT_H
#define GM2_EFFECT_H

#include "gm2.h"
#include "gm2_effects.h"
#include "gm2_process.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GM2_EFFECT_RATE 48000u
#define GM2_EFFECT_SEGMENT 4096u
/* The host's beat in ms: native adapters pass it as beat_ms, firmware adapters
 * write it into the device's global Tap Time. */
#define GM2_EFFECT_BEAT_MS 500u
/* Frames a delay-time knob's max segment holds: 24 segments, 2.05 s. */
#define GM2_EFFECT_HOLD 98304u

typedef struct {
  const char *name;    /* as the firmware descriptor prints it */
  int min;
  int max;
  int def;
  uint32_t max_frames; /* frames its max segment holds; 0 for GM2_EFFECT_SEGMENT */
} gm2_knob;

/* The Mix knob's descriptor name ends in the three glyph codes its icon shows as;
 * the name compare keeps only the 7-bit codes. */
#define GM2_MIX_GLYPHS "\x01\x02\x03"

typedef struct {
  const char *name; /* fixture stem */
  unsigned knobs;
  const gm2_knob *knob;
  uint32_t setup_words; /* coefficient words per setup record */
  /* The native setup words for one knob vector: 0 on success. */
  int (*setup)(const int32_t *values, int32_t *words);
  /* 0 when the setup refuses each NULL pointer and a context without a rate. */
  int (*rejects)(void);
  /* The block's level words for one knob vector: the input word and, for an effect
   * with a Mix knob, the wet and dry words its level code stages. 0 on success. */
  int (*levels)(const int32_t *values, int32_t level[3]);
  /* An instance set up from one knob vector, NULL on failure. */
  void *(*open)(const int32_t *values);
  int (*set_knob)(void *instance, unsigned knob, int value);
  void (*split)(void *instance, const int32_t *left, const int32_t *right, int32_t *out_left,
                int32_t *out_right, size_t frames);
  void (*f32)(void *instance, const float *left, const float *right, float *out_left,
              float *out_right, size_t frames);
  void (*interleaved)(void *instance, const int32_t *in, int32_t *out, size_t frames);
  void (*frame)(void *instance, const int32_t in[2], int32_t out[2]);
  /* The effect's align call; NULL for an effect without an LFO. */
  void (*align)(void *instance, double beats);
  void (*close)(void *instance);
} gm2_effect;

/* What a config and a state hold before the library writes them: the setup and
 * state_init have to write every word an output depends on. */
#define GM2_EFFECT_POISON 0xA5

/* The native adapter of one effect and its gm2_effect_<fixture> object, through
 * the interface every library module gm2_<module>_* has. The file gives the knob
 * table and two functions:
 *
 *   void raw(const int32_t *values, gm2_<module>_setup_raw *raw)
 *       one knob vector, in table order, as the module's raw values
 *   void words(const gm2_<module>_config *config, int32_t *words)
 *       the config's setup words, in the order the firmware adapter reads them
 *
 * GM2_EFFECT_DEFINE_LFO is the same for a module with an align call and
 * GM2_EFFECT_DEFINE_MIX for one whose Mix knob sets its level words;
 * GM2_EFFECT_DEFINE_LFO_MIX takes both. The two Mix variants name the pair with
 * one more function:
 *
 *   void mix(const gm2_<module>_config *config, int32_t *wet, int32_t *dry)
 *       where the config keeps it, in its own fields or in its core's
 */
#define GM2_EFFECT_DEFINE(fixture, module, knob_table, raw, words, word_count)                     \
  GM2_EFFECT_ADAPTER_(fixture, module, knob_table, raw, words)                                     \
  GM2_EFFECT_LEVELS_FIXED_(fixture)                                                                \
  GM2_EFFECT_OBJECT_(fixture, knob_table, word_count, NULL, fixture##_levels_fixed)

#define GM2_EFFECT_DEFINE_LFO(fixture, module, knob_table, raw, words, word_count)                 \
  GM2_EFFECT_ADAPTER_(fixture, module, knob_table, raw, words)                                     \
  GM2_EFFECT_ALIGN_(fixture, module)                                                               \
  GM2_EFFECT_LEVELS_FIXED_(fixture)                                                                \
  GM2_EFFECT_OBJECT_(fixture, knob_table, word_count, fixture##_align, fixture##_levels_fixed)

#define GM2_EFFECT_DEFINE_MIX(fixture, module, knob_table, raw, words, mix, word_count)            \
  GM2_EFFECT_ADAPTER_(fixture, module, knob_table, raw, words)                                     \
  GM2_EFFECT_LEVELS_MIX_(fixture, module, mix)                                                     \
  GM2_EFFECT_OBJECT_(fixture, knob_table, word_count, NULL, fixture##_levels_mix)

#define GM2_EFFECT_DEFINE_LFO_MIX(fixture, module, knob_table, raw, words, mix, word_count)        \
  GM2_EFFECT_ADAPTER_(fixture, module, knob_table, raw, words)                                     \
  GM2_EFFECT_ALIGN_(fixture, module)                                                               \
  GM2_EFFECT_LEVELS_MIX_(fixture, module, mix)                                                     \
  GM2_EFFECT_OBJECT_(fixture, knob_table, word_count, fixture##_align, fixture##_levels_mix)

#define GM2_EFFECT_KNOBS_(knob_table) (sizeof(knob_table) / sizeof(knob_table)[0])

#define GM2_EFFECT_ADAPTER_(fixture, module, knob_table, raw, words)                               \
  typedef struct {                                                                                 \
    gm2_context context;                                                                           \
    gm2_##module##_config config;                                                                  \
    gm2_##module##_state state;                                                                    \
    int32_t values[GM2_EFFECT_KNOBS_(knob_table)];                                                 \
  } fixture##_native;                                                                              \
                                                                                                   \
  static int fixture##_config(const int32_t *values, const gm2_context *context,                   \
                              gm2_##module##_config *config) {                                     \
    gm2_##module##_setup_raw raw_values;                                                           \
    raw(values, &raw_values);                                                                      \
    return gm2_##module##_setup_config(context, &raw_values, config);                              \
  }                                                                                                \
                                                                                                   \
  static int fixture##_setup(const int32_t *values, int32_t *setup_words) {                        \
    gm2_context context;                                                                           \
    gm2_##module##_config config;                                                                  \
    memset(&config, GM2_EFFECT_POISON, sizeof config);                                             \
    if (gm2_context_init(&context, GM2_EFFECT_RATE) != 0 ||                                        \
        fixture##_config(values, &context, &config) != 0) {                                        \
      return -1;                                                                                   \
    }                                                                                              \
    words(&config, setup_words);                                                                   \
    return 0;                                                                                      \
  }                                                                                                \
                                                                                                   \
  static int fixture##_rejects(void) {                                                             \
    int32_t values[GM2_EFFECT_KNOBS_(knob_table)] = {0};                                           \
    for (size_t knob = 0; knob < GM2_EFFECT_KNOBS_(knob_table); knob++) {                          \
      values[knob] = (knob_table)[knob].def;                                                       \
    }                                                                                              \
    gm2_context context;                                                                           \
    const gm2_context no_rate = {0};                                                               \
    gm2_##module##_setup_raw raw_values;                                                           \
    gm2_##module##_config config;                                                                  \
    raw(values, &raw_values);                                                                      \
    if (gm2_context_init(&context, GM2_EFFECT_RATE) != 0) {                                        \
      return -1;                                                                                   \
    }                                                                                              \
    return gm2_##module##_setup_config(NULL, &raw_values, &config) == -1 &&                        \
                   gm2_##module##_setup_config(&no_rate, &raw_values, &config) == -1 &&            \
                   gm2_##module##_setup_config(&context, NULL, &config) == -1 &&                   \
                   gm2_##module##_setup_config(&context, &raw_values, NULL) == -1                  \
               ? 0                                                                                 \
               : -1;                                                                               \
  }                                                                                                \
                                                                                                   \
  static void *fixture##_open(const int32_t *values) {                                             \
    fixture##_native *self = malloc(sizeof *self);                                                 \
    if (self == NULL) {                                                                            \
      return NULL;                                                                                 \
    }                                                                                              \
    memset(self, GM2_EFFECT_POISON, sizeof *self);                                                 \
    memcpy(self->values, values, sizeof self->values);                                             \
    if (gm2_context_init(&self->context, GM2_EFFECT_RATE) != 0 ||                                  \
        fixture##_config(self->values, &self->context, &self->config) != 0) {                      \
      free(self);                                                                                  \
      return NULL;                                                                                 \
    }                                                                                              \
    gm2_##module##_state_init(&self->state, &self->config);                                        \
    return self;                                                                                   \
  }                                                                                                \
                                                                                                   \
  static int fixture##_set_knob(void *instance, unsigned knob, int value) {                        \
    fixture##_native *self = instance;                                                             \
    const int32_t previous = self->values[knob];                                                   \
    gm2_##module##_config config;                                                                  \
    self->values[knob] = (int32_t)value;                                                           \
    memset(&config, GM2_EFFECT_POISON, sizeof config);                                             \
    if (fixture##_config(self->values, &self->context, &config) != 0) {                            \
      self->values[knob] = previous;                                                               \
      return -1;                                                                                   \
    }                                                                                              \
    self->config = config;                                                                         \
    return 0;                                                                                      \
  }                                                                                                \
                                                                                                   \
  static void fixture##_split(void *instance, const int32_t *left, const int32_t *right,           \
                              int32_t *out_left, int32_t *out_right, size_t frames) {              \
    fixture##_native *self = instance;                                                             \
    gm2_##module##_process_split(&self->state, &self->config, left, right, out_left, out_right,    \
                                 frames);                                                          \
  }                                                                                                \
                                                                                                   \
  static void fixture##_f32(void *instance, const float *left, const float *right,                 \
                            float *out_left, float *out_right, size_t frames) {                    \
    fixture##_native *self = instance;                                                             \
    gm2_##module##_process_f32(&self->state, &self->config, left, right, out_left, out_right,      \
                               frames);                                                            \
  }                                                                                                \
                                                                                                   \
  static void fixture##_interleaved(void *instance, const int32_t *in, int32_t *out,               \
                                    size_t frames) {                                               \
    fixture##_native *self = instance;                                                             \
    gm2_##module##_process_interleaved(&self->state, &self->config, in, out, frames);              \
  }                                                                                                \
                                                                                                   \
  static void fixture##_frame(void *instance, const int32_t in[2], int32_t out[2]) {               \
    fixture##_native *self = instance;                                                             \
    gm2_##module##_process_frame(&self->state, &self->config, in, out);                            \
  }                                                                                                \
                                                                                                   \
  static void fixture##_close(void *instance) { free(instance); }

#define GM2_EFFECT_ALIGN_(fixture, module)                                                         \
  static void fixture##_align(void *instance, double beats) {                                      \
    fixture##_native *self = instance;                                                             \
    gm2_##module##_align(&self->state, &self->config, beats);                                      \
  }

/* An effect without a Mix knob: the same three words for every knob vector. */
#define GM2_EFFECT_LEVELS_FIXED_(fixture)                                                          \
  static int fixture##_levels_fixed(const int32_t *values, int32_t level[3]) {                     \
    (void)values;                                                                                  \
    level[0] = GM2_LEVEL_INPUT;                                                                    \
    level[1] = GM2_LEVEL_WET;                                                                      \
    level[2] = 0;                                                                                  \
    return 0;                                                                                      \
  }

/* An effect whose Mix knob sets its level words: the effect names the pair, and a
 * setup that refuses is a broken adapter, never a fall back to a full mix. */
#define GM2_EFFECT_LEVELS_MIX_(fixture, module, mix)                                               \
  static int fixture##_levels_mix(const int32_t *values, int32_t level[3]) {                       \
    gm2_context context;                                                                           \
    gm2_##module##_config config;                                                                  \
    if (gm2_context_init(&context, GM2_EFFECT_RATE) != 0 ||                                        \
        fixture##_config(values, &context, &config) != 0) {                                        \
      return -1;                                                                                   \
    }                                                                                              \
    level[0] = GM2_LEVEL_INPUT;                                                                    \
    mix(&config, &level[1], &level[2]);                                                            \
    return 0;                                                                                      \
  }

#define GM2_EFFECT_OBJECT_(fixture, knob_table, word_count, align_call, levels_call)               \
  const gm2_effect gm2_effect_##fixture = {                                                        \
      .name = #fixture,                                                                            \
      .knobs = (unsigned)GM2_EFFECT_KNOBS_(knob_table),                                            \
      .knob = (knob_table),                                                                        \
      .setup_words = (word_count),                                                                 \
      .setup = fixture##_setup,                                                                    \
      .rejects = fixture##_rejects,                                                                \
      .levels = levels_call,                                                                       \
      .open = fixture##_open,                                                                      \
      .set_knob = fixture##_set_knob,                                                              \
      .split = fixture##_split,                                                                    \
      .f32 = fixture##_f32,                                                                        \
      .interleaved = fixture##_interleaved,                                                        \
      .frame = fixture##_frame,                                                                    \
      .align = align_call,                                                                         \
      .close = fixture##_close,                                                                    \
  };

/* Frames between the fast turns: short enough that each lands while the glide
 * the turn before started still moves. */
#define GM2_EFFECT_FAST 64u
#define GM2_EFFECT_FAST_TURNS 10u

/* `knob` is the knob the segment sets to `value`, or GM2_ALL for the defaults. */
#define GM2_ALL (-1)

typedef struct {
  int knob;
  int value;
  uint32_t frames;
} gm2_segment;

#define GM2_EFFECT_SEGMENTS(knobs) (5u + (10u + 2u * GM2_EFFECT_FAST_TURNS) * (knobs))

static inline int gm2_knob_clamp(const gm2_knob *knob, int value) {
  return value < knob->min ? knob->min : value > knob->max ? knob->max : value;
}

static inline int gm2_knob_at(const gm2_knob *knob, unsigned num, unsigned den) {
  const unsigned span = (unsigned)(knob->max - knob->min);
  return knob->min + (int)((span * num + den / 2u) / den);
}

/* An active non-default setting for multi-knob runs: Tempo at Ignore so Speed
 * and Delay are unmasked, two-state switches flipped from their default, and
 * continuous knobs at num/den of their span. */
static inline int gm2_knob_active(const gm2_knob *knob, unsigned num, unsigned den) {
  if (knob->min == 0 && knob->max == 16) {
    return 0;
  }
  if (knob->min == 0 && knob->max == 1) {
    return 1 - knob->def;
  }
  return gm2_knob_at(knob, num, den);
}

/* Defaults; for each knob its min, 33 %, 66 %, max and default; two more at the
 * defaults; then the multi-knob active walk, sweep and fast turns; a walk back
 * to the defaults; then ten fast turns per knob from the defaults; and the
 * defaults again. Segment i runs i % 4 frames past its length, so the turns land
 * on every frame of a four-frame chunk. Returns the count written, 0 when `cap`
 * is too small. */
static inline unsigned gm2_effect_segments(const gm2_effect *effect, gm2_segment *segment,
                                           unsigned cap) {
  if (cap < GM2_EFFECT_SEGMENTS(effect->knobs)) {
    return 0;
  }
  unsigned n = 0;
  segment[n++] = (gm2_segment){GM2_ALL, 0, GM2_EFFECT_SEGMENT};
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    const gm2_knob *record = &effect->knob[knob];
    uint32_t longest = record->max_frames != 0u ? record->max_frames : GM2_EFFECT_SEGMENT;
    segment[n++] = (gm2_segment){(int)knob, record->min, GM2_EFFECT_SEGMENT};
    segment[n++] = (gm2_segment){(int)knob, gm2_knob_at(record, 1u, 3u), GM2_EFFECT_SEGMENT};
    segment[n++] = (gm2_segment){(int)knob, gm2_knob_at(record, 2u, 3u), GM2_EFFECT_SEGMENT};
    segment[n++] = (gm2_segment){(int)knob, record->max, longest};
    segment[n++] = (gm2_segment){(int)knob, record->def, GM2_EFFECT_SEGMENT};
  }
  segment[n++] = (gm2_segment){GM2_ALL, 0, GM2_EFFECT_SEGMENT};
  segment[n++] = (gm2_segment){GM2_ALL, 0, GM2_EFFECT_SEGMENT};

  for (unsigned step = effect->knobs; step > 0u; step--) {
    const unsigned knob = step - 1u;
    const gm2_knob *record = &effect->knob[knob];
    segment[n++] = (gm2_segment){(int)knob, gm2_knob_active(record, 2u, 3u), GM2_EFFECT_SEGMENT};
  }
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    const gm2_knob *record = &effect->knob[knob];
    uint32_t longest = record->max_frames != 0u ? record->max_frames : GM2_EFFECT_SEGMENT;
    segment[n++] = (gm2_segment){(int)knob, gm2_knob_at(record, 1u, 3u), GM2_EFFECT_SEGMENT};
    segment[n++] = (gm2_segment){(int)knob, record->max, longest};
    segment[n++] = (gm2_segment){(int)knob, gm2_knob_active(record, 2u, 3u), GM2_EFFECT_SEGMENT};
  }
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    const gm2_knob *record = &effect->knob[knob];
    const int active = gm2_knob_active(record, 2u, 3u);
    const int fast[GM2_EFFECT_FAST_TURNS] = {
        active + 1,
        active + 2,
        active + 3,
        gm2_knob_at(record, 1u, 3u),
        gm2_knob_at(record, 2u, 3u),
        record->max,
        record->min,
        record->min + 1,
        record->min + 2,
        active,
    };
    for (unsigned turn = 0; turn < GM2_EFFECT_FAST_TURNS; turn++) {
      segment[n++] = (gm2_segment){(int)knob, gm2_knob_clamp(record, fast[turn]), GM2_EFFECT_FAST};
    }
  }
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    const gm2_knob *record = &effect->knob[knob];
    segment[n++] = (gm2_segment){(int)knob, record->def, GM2_EFFECT_FAST};
  }

  segment[n++] = (gm2_segment){GM2_ALL, 0, GM2_EFFECT_SEGMENT};
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    const gm2_knob *record = &effect->knob[knob];
    const int def = record->def;
    const int fast[GM2_EFFECT_FAST_TURNS] = {
        def + 1,
        def + 2,
        def + 3,
        gm2_knob_at(record, 1u, 3u),
        gm2_knob_at(record, 2u, 3u),
        record->max,
        record->min,
        record->min + 1,
        record->min + 2,
        def,
    };
    for (unsigned turn = 0; turn < GM2_EFFECT_FAST_TURNS; turn++) {
      segment[n++] = (gm2_segment){(int)knob, gm2_knob_clamp(record, fast[turn]), GM2_EFFECT_FAST};
    }
  }
  segment[n++] = (gm2_segment){GM2_ALL, 0, GM2_EFFECT_SEGMENT};
  for (unsigned i = 0; i < n; i++) {
    segment[i].frames += i % 4u;
  }
  return n;
}

/* Every knob at its default, the setting a fixture's level words describe. */
static inline void gm2_effect_defaults(const gm2_effect *effect, int32_t *values) {
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    values[knob] = effect->knob[knob].def;
  }
}

/* The knob vector a segment runs with: its own value with the others carried
 * over from the segment before, or every knob at its default for GM2_ALL. */
static inline void gm2_effect_values(const gm2_effect *effect, const gm2_segment *segment,
                                     int32_t *values) {
  if (segment->knob == GM2_ALL) {
    gm2_effect_defaults(effect, values);
  } else {
    values[segment->knob] = segment->value;
  }
}

#define GM2_EFFECT_DECLARE(name) extern const gm2_effect gm2_effect_##name;
GM2_EFFECTS(GM2_EFFECT_DECLARE)
#undef GM2_EFFECT_DECLARE

#endif /* GM2_EFFECT_H */
