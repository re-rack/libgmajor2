/* Fixture recorder CLI: gm2_record <name> <path>, where <name> is an effect,
 * "swf" or "tables", or --list. The emulator is linked in; nothing else here knows
 * about it. */
#include "gm2_record.h"

#include "gm2_fixture.h"
#include "gm2_process.h"
#include "gm2_signal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const gm2_firmware_ops *const k_firmware[] = {
#define GM2_FIRMWARE_ENTRY(name) &gm2_firmware_##name,
    GM2_EFFECTS(GM2_FIRMWARE_ENTRY)
#undef GM2_FIRMWARE_ENTRY
};

#define FIRMWARE_COUNT (sizeof k_firmware / sizeof k_firmware[0])

static int vector_present(const int32_t *vectors, unsigned count, unsigned knobs,
                          const int32_t *candidate) {
  for (unsigned vector = 0; vector < count; vector++) {
    unsigned knob = 0;
    while (knob < knobs && vectors[vector * knobs + knob] == candidate[knob]) {
      knob++;
    }
    if (knob == knobs) {
      return 1;
    }
  }
  return 0;
}

/* Each knob on its own, the others at default: every position when the knob has
 * 32 or fewer, else 32 spread evenly plus the default. Then four multi-knob
 * presets - every knob active at 33 %, at 66 %, at its minimum and at its
 * maximum - each built one knob at a time and each stepped through its six
 * points. */
static unsigned setup_vectors(const gm2_effect *effect, int32_t *vectors, unsigned cap) {
  unsigned knobs = effect->knobs;
  int32_t *candidate = malloc(knobs * sizeof *candidate);
  int32_t *target = malloc(knobs * sizeof *target);
  if (candidate == NULL || target == NULL) {
    free(candidate);
    free(target);
    return 0;
  }
  for (unsigned knob = 0; knob < knobs; knob++) {
    candidate[knob] = effect->knob[knob].def;
  }
  memcpy(vectors, candidate, knobs * sizeof *candidate);
  unsigned count = 1;

  for (unsigned knob = 0; knob < knobs; knob++) {
    const gm2_knob *record = &effect->knob[knob];
    int span = record->max - record->min;
    unsigned positions = span + 1 <= 32 ? (unsigned)(span + 1) : 32u;
    for (unsigned i = 0; i < positions && count < cap; i++) {
      int value;
      if (span + 1 <= 32) {
        value = record->min + (int)i;
      } else {
        value = record->min + (int)((i * (unsigned)span + 15u) / 31u);
      }
      candidate[knob] = value;
      if (!vector_present(vectors, count, knobs, candidate)) {
        memcpy(vectors + count * knobs, candidate, knobs * sizeof *candidate);
        count++;
      }
    }
    candidate[knob] = record->def;
    if (count < cap && !vector_present(vectors, count, knobs, candidate)) {
      memcpy(vectors + count * knobs, candidate, knobs * sizeof *candidate);
      count++;
    }
  }

  for (unsigned preset = 0; preset < 4u; preset++) {
    for (unsigned knob = 0; knob < knobs; knob++) {
      const gm2_knob *record = &effect->knob[knob];
      if (preset == 0u) {
        target[knob] = gm2_knob_active(record, 1u, 3u);
      } else if (preset == 1u) {
        target[knob] = gm2_knob_active(record, 2u, 3u);
      } else if (preset == 2u) {
        target[knob] = record->min;
      } else {
        target[knob] = record->max;
      }
    }
    for (unsigned step = knobs; step > 0u; step--) {
      const unsigned knob = step - 1u;
      candidate[knob] = target[knob];
      if (count < cap && !vector_present(vectors, count, knobs, candidate)) {
        memcpy(vectors + count * knobs, candidate, knobs * sizeof *candidate);
        count++;
      }
    }
    for (unsigned knob = 0; knob < knobs; knob++) {
      const gm2_knob *record = &effect->knob[knob];
      const int points[6] = {record->min,
                             gm2_knob_at(record, 1u, 3u),
                             gm2_knob_at(record, 2u, 3u),
                             record->max,
                             record->def,
                             target[knob]};
      for (unsigned point = 0; point < 6u; point++) {
        candidate[knob] = points[point];
        if (count < cap && !vector_present(vectors, count, knobs, candidate)) {
          memcpy(vectors + count * knobs, candidate, knobs * sizeof *candidate);
          count++;
        }
      }
      candidate[knob] = target[knob];
    }
  }
  free(candidate);
  free(target);
  return count;
}

int gm2_record_effect(const gm2_firmware_ops *ops, const char *path) {
  const gm2_effect *effect = ops->effect;
  unsigned knobs = effect->knobs;
  unsigned setup_count = 0;
  unsigned segments = 0;
  unsigned long frames = 0;
  unsigned char *record = NULL;
  int32_t *values = NULL;
  int32_t *vectors = NULL;
  gm2_segment *segment = NULL;
  gm2_fixture_writer writer;
  int writer_open = 0;
  int32_t levels[3] = {0, 0, 0};
  gm2_firmware *firmware = ops->open();
  if (firmware == NULL) {
    return -1;
  }
  if (ops->levels(firmware, levels) != 0) {
    goto fail;
  }

  unsigned max_vectors = 1u + knobs * 64u;
  values = malloc((knobs + effect->setup_words) * sizeof *values);
  vectors = malloc(max_vectors * knobs * sizeof *vectors);
  record = malloc(4u * (knobs + effect->setup_words));
  segment = malloc((GM2_EFFECT_SEGMENTS(knobs)) * sizeof *segment);
  if (values == NULL || vectors == NULL || record == NULL || segment == NULL) {
    goto fail;
  }

  if (gm2_fixture_writer_open(&writer, path) != 0) {
    goto fail;
  }
  writer_open = 1;

  for (unsigned knob = 0; knob < knobs; knob++) {
    gm2_fixture_knob entry;
    memset(&entry, 0, sizeof entry);
    size_t length = strlen(effect->knob[knob].name);
    if (length >= sizeof entry.name) {
      fprintf(stderr, "%s: knob name %s is too long\n", effect->name, effect->knob[knob].name);
      goto fail;
    }
    memcpy(entry.name, effect->knob[knob].name, length + 1u);
    entry.min = effect->knob[knob].min;
    entry.max = effect->knob[knob].max;
    entry.def = effect->knob[knob].def;
    unsigned char bytes[GM2_FIX_KNOBS_SIZE];
    gm2_fixture_put_knob(bytes, &entry);
    if (gm2_fixture_writer_add(&writer, GM2_FIX_KNOBS, sizeof bytes, bytes) != 0) {
      goto fail;
    }
  }

  {
    int32_t want[3] = {0, 0, 0};
    gm2_effect_defaults(effect, values);
    if (effect->levels(values, want) != 0) {
      fprintf(stderr, "%s: the level words do not build from the defaults\n", effect->name);
      goto fail;
    }
    for (unsigned i = 0; i < 3u; i++) {
      if (levels[i] != want[i]) {
        fprintf(stderr, "%s: level word %u is %06X, the effect fixes %06X\n", effect->name, i,
                (uint32_t)levels[i] & 0xFFFFFFu, (uint32_t)want[i] & 0xFFFFFFu);
        goto fail;
      }
    }
    unsigned char bytes[GM2_FIX_LEVELS_SIZE];
    gm2_fixture_put_i32(bytes, 3, levels);
    if (gm2_fixture_writer_add(&writer, GM2_FIX_LEVELS, sizeof bytes, bytes) != 0) {
      goto fail;
    }
  }

  /* The instance is at the defaults, so the first vector turns nothing. */
  unsigned count = setup_vectors(effect, vectors, max_vectors);
  if (count == 0u) {
    goto fail;
  }
  for (unsigned knob = 0; knob < knobs; knob++) {
    values[knob] = effect->knob[knob].def;
  }
  for (unsigned vector = 0; vector < count; vector++) {
    const int32_t *want = vectors + (size_t)vector * knobs;
    for (unsigned knob = 0; knob < knobs; knob++) {
      if (want[knob] == values[knob]) {
        continue;
      }
      if (ops->set_knob(firmware, knob, (int)want[knob]) != 0) {
        goto fail;
      }
      values[knob] = want[knob];
    }
    gm2_fixture_put_i32(record, knobs, want);
    if (ops->words(firmware, values + knobs) != 0) {
      goto fail;
    }
    gm2_fixture_put_i32(record + 4u * knobs, effect->setup_words, values + knobs);
    if (gm2_fixture_writer_add(&writer, GM2_FIX_SETUP, 4u * (knobs + effect->setup_words),
                               record) != 0) {
      goto fail;
    }
    setup_count++;
  }

  ops->close(firmware);
  firmware = ops->open();
  if (firmware == NULL) {
    goto fail;
  }
  int32_t *current = vectors; /* the setup vectors are done with */
  for (unsigned knob = 0; knob < knobs; knob++) {
    current[knob] = effect->knob[knob].def;
    values[knob] = effect->knob[knob].def;
  }
  segments = gm2_effect_segments(effect, segment, GM2_EFFECT_SEGMENTS(knobs));
  if (segments == 0u) {
    goto fail;
  }
  gm2_signal signal;
  gm2_signal_init(&signal);
  for (unsigned index = 0; index < segments; index++) {
    gm2_effect_values(effect, &segment[index], values);
    for (unsigned knob = 0; knob < knobs; knob++) {
      if (values[knob] == current[knob]) {
        continue;
      }
      if (ops->set_knob(firmware, knob, (int)values[knob]) != 0) {
        goto fail;
      }
      current[knob] = values[knob];
    }
    gm2_fixture_put_segment(record, knobs, values, segment[index].frames);
    if (gm2_fixture_writer_add(&writer, GM2_FIX_SEGMENT, 4u * (knobs + 1u), record) != 0) {
      goto fail;
    }
    for (uint32_t frame = 0; frame < segment[index].frames; frame++) {
      int32_t left = 0;
      int32_t right = 0;
      int32_t out_left = 0;
      int32_t out_right = 0;
      gm2_signal_next(&signal, &left, &right);
      if (ops->frame(firmware, left, right, &out_left, &out_right) != 0) {
        fprintf(stderr, "%s: audio frame %lu never reached the stop\n", effect->name,
                frames + frame);
        goto fail;
      }
      gm2_fixture_put_output(record, out_left, out_right);
      if (gm2_fixture_writer_add(&writer, GM2_FIX_OUTPUT, GM2_FIX_OUTPUT_SIZE, record) != 0) {
        goto fail;
      }
    }
    frames += segment[index].frames;
  }

  if (gm2_fixture_writer_close(&writer) != 0) {
    fprintf(stderr, "%s: cannot write %s\n", effect->name, path);
    goto fail;
  }
  printf("%-15s recorded %u settings, %u segments, %lu frames to %s\n", effect->name, setup_count,
         segments, frames, path);
  free(values);
  free(vectors);
  free(record);
  free(segment);
  ops->close(firmware);
  return 0;

fail:
  if (writer_open != 0) {
    gm2_fixture_writer_abort(&writer);
  }
  free(values);
  free(vectors);
  free(record);
  free(segment);
  if (firmware != NULL) {
    ops->close(firmware);
  }
  return -1;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--list") == 0) {
    for (size_t i = 0; i < FIRMWARE_COUNT; i++) {
      puts(k_firmware[i]->effect->name);
    }
    puts("swf");
    puts("tables");
    return 0;
  }
  if (argc != 3) {
    fprintf(stderr, "usage: %s <name> <path>\n       %s --list\n", argv[0], argv[0]);
    return 2;
  }
  if (strcmp(argv[1], "swf") == 0) {
    return gm2_record_swf(argv[2]) == 0 ? 0 : 1;
  }
  if (strcmp(argv[1], "tables") == 0) {
    return gm2_record_tables(argv[2]) == 0 ? 0 : 1;
  }
  for (size_t i = 0; i < FIRMWARE_COUNT; i++) {
    if (strcmp(argv[1], k_firmware[i]->effect->name) == 0) {
      return gm2_record_effect(k_firmware[i], argv[2]) == 0 ? 0 : 1;
    }
  }
  fprintf(stderr, "%s: nothing to record is called %s\n", argv[0], argv[1]);
  return 2;
}
