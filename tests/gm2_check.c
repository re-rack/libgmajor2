/* The checker: replays every fixture's firmware answers through the native
 * library, offline, and checks the host-facing additions the firmware has no
 * answer for. Prints one PASS or FAIL line per item, at most five detail lines
 * per item, and exits non-zero when anything failed. */
#include "gm2_convert.h"
#include "gm2_delay_design.h"
#include "gm2_effect.h"
#include "gm2_eq_setup.h"
#include "gm2_fixture.h"
#include "gm2_lfo.h"
#include "gm2_process.h"
#include "gm2_signal.h"
#include "gm2_swf.h"
#include "gm2_tables.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIXTURE_DIR "tests/fixtures"
#define DETAIL_MAX 5u
#define CHUNK_MAX 1024u

static unsigned item_failures;
static unsigned item_shown;

static void __attribute__((format(printf, 1, 2))) detail(const char *format, ...) {
  va_list args;
  if (item_shown >= DETAIL_MAX) {
    return;
  }
  item_shown++;
  fputs("     ", stdout);
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  putchar('\n');
}

static void failed(void) { item_failures++; }

static uint32_t next_random(uint32_t *state) {
  *state = *state * 1664525u + 1013904223u;
  return *state >> 8;
}

/* The knobs a setting moves from the defaults, for a failure message: up to the
 * first two, and how many more there are. */
static void segment_label(const gm2_effect *effect, const int32_t *values, char *buffer,
                          size_t bytes) {
  unsigned changed[2] = {0, 0};
  unsigned count = 0;
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    if (values[knob] != effect->knob[knob].def) {
      if (count < 2u) {
        changed[count] = knob;
      }
      count++;
    }
  }
  if (count == 0u) {
    snprintf(buffer, bytes, "defaults");
  } else if (count == 1u) {
    snprintf(buffer, bytes, "%.24s = %d", effect->knob[changed[0]].name, (int)values[changed[0]]);
  } else if (count == 2u) {
    snprintf(buffer, bytes, "%.20s = %d, %.20s = %d", effect->knob[changed[0]].name,
             (int)values[changed[0]], effect->knob[changed[1]].name, (int)values[changed[1]]);
  } else {
    snprintf(buffer, bytes, "%.20s = %d (+%u)", effect->knob[changed[0]].name,
             (int)values[changed[0]], count - 1u);
  }
}

static int check_knobs(const gm2_effect *effect, const gm2_fixture *fixture) {
  uint32_t count = 0;
  uint32_t size = 0;
  const void *records = gm2_fixture_records(fixture, GM2_FIX_KNOBS, &count, &size);
  int ok = records != NULL && count == effect->knobs;
  for (unsigned knob = 0; ok && knob < effect->knobs; knob++) {
    gm2_fixture_knob entry;
    gm2_fixture_get_knob((const unsigned char *)records + (size_t)size * knob, &entry);
    const gm2_knob *want = &effect->knob[knob];
    ok = strcmp(entry.name, want->name) == 0 && entry.min == want->min && entry.max == want->max &&
         entry.def == want->def;
  }
  if (!ok) {
    detail("fixture out of date: run make record-fixture FIXTURE=%s", effect->name);
  }
  return ok;
}

/* The level words the firmware's block staged at the defaults are the ones the
 * effect's knobs fix. */
static int check_levels(const gm2_effect *effect, const gm2_fixture *fixture) {
  uint32_t count = 0;
  uint32_t size = 0;
  const void *records = gm2_fixture_records(fixture, GM2_FIX_LEVELS, &count, &size);
  if (records == NULL || count != 1u) {
    detail("the fixture holds no level words");
    failed();
    return 0;
  }
  int32_t *values = malloc((size_t)effect->knobs * sizeof *values);
  int32_t want[3] = {0, 0, 0};
  int32_t words[3] = {0, 0, 0};
  if (values == NULL) {
    detail("out of memory");
    failed();
    return 0;
  }
  gm2_effect_defaults(effect, values);
  int ok = 1;
  if (effect->levels(values, want) != 0) {
    detail("the level words do not build from the defaults");
    failed();
    ok = 0;
  }
  free(values);
  gm2_fixture_get_i32(records, 3, words);
  for (unsigned i = 0; i < 3u; i++) {
    if (words[i] != want[i]) {
      detail("level word %u is %d, the effect fixes %d", i, (int)words[i], (int)want[i]);
      failed();
      ok = 0;
    }
  }
  return ok;
}

static unsigned check_setup(const gm2_effect *effect, const gm2_fixture *fixture) {
  uint32_t count = 0;
  uint32_t size = 0;
  const void *records = gm2_fixture_records(fixture, GM2_FIX_SETUP, &count, &size);
  unsigned knobs = effect->knobs;
  unsigned words = effect->setup_words;
  if (records == NULL || count == 0u || size != 4u * (knobs + words)) {
    detail("the fixture holds no usable setup records");
    failed();
    return 0;
  }
  int32_t *values = malloc((knobs + words) * sizeof *values);
  int32_t *mine = malloc(words * sizeof *mine);
  if (values == NULL || mine == NULL) {
    detail("out of memory");
    failed();
    free(values);
    free(mine);
    return 0;
  }
  for (uint32_t record = 0; record < count; record++) {
    gm2_fixture_get_i32((const unsigned char *)records + (size_t)size * record, knobs + words,
                        values);
    if (effect->setup(values, mine) != 0) {
      char label[64];
      segment_label(effect, values, label, sizeof label);
      detail("setting %u (%s) has no native setup", record, label);
      failed();
      continue;
    }
    for (unsigned word = 0; word < words; word++) {
      uint32_t want = (uint32_t)values[knobs + word] & 0xFFFFFFu;
      uint32_t got = (uint32_t)mine[word] & 0xFFFFFFu;
      if (want != got) {
        char label[64];
        segment_label(effect, values, label, sizeof label);
        detail("setting %u (%s) word %u: %06X, firmware %06X", record, label, word, got, want);
        failed();
        break;
      }
    }
  }
  free(values);
  free(mine);
  return count;
}

typedef struct {
  const gm2_effect *effect;
  const int32_t *values; /* knobs per segment */
  const uint32_t *frames;
  unsigned segments;
  const int32_t *q23_left;
  const int32_t *q23_right;
  float *f32_left;
  float *f32_right;
  const unsigned char *outputs;
} audio_job;

/* The entry points one run replays: the two planar ones and the interleaved one
 * chunk alike, the frame one a frame at a time. */
typedef enum {
  AUDIO_SPLIT,
  AUDIO_FLOAT,
  AUDIO_INTERLEAVED,
  AUDIO_FRAME,
} audio_mode;

static const char *audio_mode_name(audio_mode mode) {
  switch (mode) {
  case AUDIO_FLOAT:
    return "float";
  case AUDIO_INTERLEAVED:
    return "interleaved";
  case AUDIO_FRAME:
    return "frame";
  case AUDIO_SPLIT:
  default:
    return "24-bit";
  }
}

/* One chunked pass over the whole run through one entry point. */
static unsigned audio_run(const audio_job *job, audio_mode mode, uint32_t seed) {
  const gm2_effect *effect = job->effect;
  unsigned knobs = effect->knobs;
  unsigned mismatches = 0;
  int32_t *scratch_left = malloc(CHUNK_MAX * sizeof *scratch_left);
  int32_t *scratch_right = malloc(CHUNK_MAX * sizeof *scratch_right);
  int32_t *pairs = malloc(2u * CHUNK_MAX * sizeof *pairs);
  int32_t *got_left = malloc(CHUNK_MAX * sizeof *got_left);
  int32_t *got_right = malloc(CHUNK_MAX * sizeof *got_right);
  int32_t *current = malloc(knobs * sizeof *current);
  if (scratch_left == NULL || scratch_right == NULL || pairs == NULL || got_left == NULL ||
      got_right == NULL || current == NULL) {
    detail("out of memory");
    failed();
    goto done;
  }
  memcpy(current, job->values, knobs * sizeof *current);
  void *instance = effect->open(current);
  if (instance == NULL) {
    detail("the effect refused its default setting");
    failed();
    goto done;
  }
  size_t position = 0;
  for (unsigned index = 0; index < job->segments; index++) {
    const int32_t *setting = job->values + (size_t)index * knobs;
    if (index != 0u) {
      for (unsigned knob = 0; knob < knobs; knob++) {
        if (setting[knob] != current[knob] &&
            effect->set_knob(instance, knob, setting[knob]) != 0) {
          detail("the effect refused %s = %d", effect->knob[knob].name, (int)setting[knob]);
          failed();
        }
      }
      memcpy(current, setting, knobs * sizeof *current);
    }
    size_t end = position + job->frames[index];
    while (position < end) {
      size_t chunk = 1u + next_random(&seed) % CHUNK_MAX;
      if (chunk > end - position) {
        chunk = end - position;
      }
      /* The paired entry points also prove that input bits above bit 23 are ignored. */
      const int paired = mode == AUDIO_INTERLEAVED || mode == AUDIO_FRAME;
      const int32_t high = paired ? 0x5A000000 : 0;
      for (size_t frame = 0; frame < chunk; frame++) {
        pairs[2u * frame] = job->q23_left[position + frame] ^ high;
        pairs[2u * frame + 1u] = job->q23_right[position + frame] ^ high;
      }
      switch (mode) {
      case AUDIO_SPLIT:
        effect->split(instance, job->q23_left + position, job->q23_right + position, scratch_left,
                      scratch_right, chunk);
        break;
      case AUDIO_FLOAT:
        effect->f32(instance, job->f32_left + position, job->f32_right + position,
                    job->f32_left + position, job->f32_right + position, chunk);
        gm2_f32_to_q23(scratch_left, job->f32_left + position, chunk);
        gm2_f32_to_q23(scratch_right, job->f32_right + position, chunk);
        break;
      case AUDIO_INTERLEAVED:
        effect->interleaved(instance, pairs, pairs, chunk);
        break;
      case AUDIO_FRAME:
        for (size_t frame = 0; frame < chunk; frame++) {
          effect->frame(instance, pairs + 2u * frame, pairs + 2u * frame);
        }
        break;
      }
      for (size_t frame = 0; frame < chunk; frame++) {
        got_left[frame] = paired != 0 ? pairs[2u * frame] : scratch_left[frame];
        got_right[frame] = paired != 0 ? pairs[2u * frame + 1u] : scratch_right[frame];
      }
      for (size_t frame = position; frame < end && frame < position + chunk; frame++) {
        int32_t want_left = 0;
        int32_t want_right = 0;
        gm2_fixture_get_output(job->outputs + frame * GM2_FIX_OUTPUT_SIZE, &want_left, &want_right);
        int32_t have_left = got_left[frame - position];
        int32_t have_right = got_right[frame - position];
        if (have_left != want_left || have_right != want_right) {
          char label[64];
          segment_label(effect, setting, label, sizeof label);
          detail("segment %u (%s) frame %zu %s: %d/%d, firmware %d/%d", index, label, frame,
                 audio_mode_name(mode), (int)have_left, (int)have_right, (int)want_left,
                 (int)want_right);
          mismatches++;
          failed();
        }
      }
      position += chunk;
    }
  }
  effect->close(instance);

done:
  free(scratch_left);
  free(scratch_right);
  free(pairs);
  free(got_left);
  free(got_right);
  free(current);
  return mismatches;
}

static size_t check_audio(const gm2_effect *effect, const gm2_fixture *fixture) {
  unsigned knobs = effect->knobs;
  uint32_t segments = 0;
  uint32_t segment_size = 0;
  uint32_t output_count = 0;
  uint32_t output_size = 0;
  const void *segment_records =
      gm2_fixture_records(fixture, GM2_FIX_SEGMENT, &segments, &segment_size);
  const void *outputs = gm2_fixture_records(fixture, GM2_FIX_OUTPUT, &output_count, &output_size);
  if (segment_records == NULL || outputs == NULL || segments == 0u || output_count == 0u ||
      output_size != GM2_FIX_OUTPUT_SIZE) {
    detail("the fixture holds no audio run");
    failed();
    return 0;
  }
  if (segment_size != 4u * (knobs + 1u)) {
    detail("segment records are %u bytes", segment_size);
    failed();
    return 0;
  }
  int32_t *values = malloc((size_t)segments * knobs * sizeof *values);
  uint32_t *frames = malloc(segments * sizeof *frames);
  if (values == NULL || frames == NULL) {
    detail("out of memory");
    failed();
    free(values);
    free(frames);
    return 0;
  }
  size_t total = 0;
  for (uint32_t index = 0; index < segments; index++) {
    uint32_t count = 0;
    gm2_fixture_get_segment((const unsigned char *)segment_records + (size_t)segment_size * index,
                            knobs, values + (size_t)index * knobs, &count);
    frames[index] = count;
    total += count;
  }
  if (total != output_count) {
    detail("the segments hold %zu frames, the outputs %u", total, output_count);
    failed();
    free(values);
    free(frames);
    return 0;
  }
  audio_job job;
  memset(&job, 0, sizeof job);
  job.effect = effect;
  job.values = values;
  job.frames = frames;
  job.segments = segments;
  job.outputs = outputs;
  int32_t *q23_left = malloc(total * sizeof *q23_left);
  int32_t *q23_right = malloc(total * sizeof *q23_right);
  float *f32_left = malloc(total * sizeof *f32_left);
  float *f32_right = malloc(total * sizeof *f32_right);
  if (q23_left == NULL || q23_right == NULL || f32_left == NULL || f32_right == NULL) {
    detail("out of memory");
    failed();
    goto done;
  }
  gm2_signal signal;
  gm2_signal_init(&signal);
  gm2_signal_fill(&signal, q23_left, q23_right, total);
  job.q23_left = q23_left;
  job.q23_right = q23_right;
  job.f32_left = f32_left;
  job.f32_right = f32_right;
  gm2_q23_to_f32(f32_left, q23_left, total);
  gm2_q23_to_f32(f32_right, q23_right, total);
  for (int mode = AUDIO_SPLIT; mode <= AUDIO_FRAME; mode++) {
    audio_run(&job, (audio_mode)mode, 0x12345678u);
  }

done:
  free(q23_left);
  free(q23_right);
  free(f32_left);
  free(f32_right);
  free(values);
  free(frames);
  return total;
}

/* Every knob's descriptor window is enforced: one step past either end must give
 * the words the end itself gives, the other knobs at their defaults. */
static void check_range(const gm2_effect *effect) {
  const gm2_segment defaults = {GM2_ALL, 0, 0};
  unsigned knobs = effect->knobs;
  unsigned words = effect->setup_words;
  int32_t *values = malloc(knobs * sizeof *values);
  int32_t *at_end = malloc(words * sizeof *at_end);
  int32_t *past_end = malloc(words * sizeof *past_end);
  if (values == NULL || at_end == NULL || past_end == NULL) {
    detail("out of memory");
    failed();
    free(values);
    free(at_end);
    free(past_end);
    return;
  }
  gm2_effect_values(effect, &defaults, values);
  for (unsigned knob = 0; knob < knobs; knob++) {
    const gm2_knob *record = &effect->knob[knob];
    for (unsigned side = 0; side < 2u; side++) {
      const int edge = side == 0u ? record->min : record->max;
      const int past = side == 0u ? record->min - 1 : record->max + 1;
      values[knob] = edge;
      const int edge_ok = effect->setup(values, at_end);
      values[knob] = past;
      const int past_ok = effect->setup(values, past_end);
      values[knob] = record->def;
      if (edge_ok != 0 || past_ok != 0) {
        detail("%s: %s = %d or %d has no setup", effect->name, record->name, edge, past);
        failed();
        continue;
      }
      for (unsigned word = 0; word < words; word++) {
        const uint32_t want = (uint32_t)at_end[word] & 0xFFFFFFu;
        const uint32_t got = (uint32_t)past_end[word] & 0xFFFFFFu;
        if (want != got) {
          detail("%s: %s = %d %06X, but = %d %06X (word %u)", effect->name, record->name, edge,
                 want, past, got, word);
          failed();
          break;
        }
      }
    }
  }
  free(values);
  free(at_end);
  free(past_end);
}

static unsigned check_effect(const gm2_effect *effect) {
  char path[128];
  int length = snprintf(path, sizeof path, "%s/%s.bin", FIXTURE_DIR, effect->name);
  gm2_fixture fixture;
  item_failures = 0;
  item_shown = 0;
  unsigned settings = 0;
  size_t frames = 0;
  if (length < 0 || (size_t)length >= sizeof path || gm2_fixture_load(&fixture, path) != 0) {
    printf("%-17s FAIL  cannot load the fixture\n", effect->name);
    return 1;
  }
  int knobs_ok = check_knobs(effect, &fixture);
  if (knobs_ok == 0) {
    failed();
  }
  check_levels(effect, &fixture);
  if (effect->rejects() != 0) {
    detail("the setup accepts a NULL pointer or a context without a rate");
    failed();
  }
  if (knobs_ok != 0) {
    check_range(effect);
    settings = check_setup(effect, &fixture);
    frames = check_audio(effect, &fixture);
  }
  gm2_fixture_free(&fixture);
  int ok = item_failures == 0;
  printf("%-17s %s  %u settings, %zu frames (split, float, interleaved and frame), descriptor "
         "windows, level words, refusals\n",
         effect->name, ok ? "PASS" : "FAIL", settings, frames);
  return ok ? 0 : 1;
}

/* --- the de-popped variants: the effect's output once a turn has settled ---- */

/* Frames after a variant's last turn by which it plays its effect's output
 * again: its longest group glide (34,560), the hand-back's longest wait and its
 * fade (gm2_handback.h), with room. Each hold then compares its last frames. */
#define ALT_SETTLE_FRAMES 140000u
#define ALT_COMPARE_FRAMES 8192u
#define ALT_SWEEP_STEPS 32u
#define ALT_SWEEP_FRAMES 128u
#define ALT_FAST_TURNS 10u
#define ALT_FAST_FRAMES 64u

typedef struct {
  const gm2_effect *effect;
  const gm2_effect *alt;
  void *exact;
  void *own;
  gm2_signal signal;
  uint32_t seed;
  size_t compared;
  size_t departed; /* frames where the variant played its own path */
  unsigned entry;  /* the entry point the next buffer goes through */
  int broken;      /* a mismatch was reported: the rest of the run is not compared */
} alt_run;

/* The variant through one of its four entry points, in turn: Q23 to float is
 * exact, so the float one's output converts back for an exact compare. */
#define ALT_ENTRY_POINTS 4u

static void alt_entry(const gm2_effect *alt, void *own, unsigned entry, const int32_t *in_l,
                      const int32_t *in_r, int32_t got[2][CHUNK_MAX], size_t count) {
  float flt[2][CHUNK_MAX];
  int32_t pair[2u * CHUNK_MAX];
  switch (entry) {
  case 0:
    alt->split(own, in_l, in_r, got[0], got[1], count);
    break;
  case 1:
    gm2_q23_to_f32(flt[0], in_l, count);
    gm2_q23_to_f32(flt[1], in_r, count);
    alt->f32(own, flt[0], flt[1], flt[0], flt[1], count);
    gm2_f32_to_q23(got[0], flt[0], count);
    gm2_f32_to_q23(got[1], flt[1], count);
    break;
  case 2:
    for (size_t i = 0; i < count; i++) {
      pair[2u * i] = in_l[i];
      pair[2u * i + 1u] = in_r[i];
    }
    alt->interleaved(own, pair, pair, count);
    for (size_t i = 0; i < count; i++) {
      got[0][i] = pair[2u * i];
      got[1][i] = pair[2u * i + 1u];
    }
    break;
  default:
    for (size_t i = 0; i < count; i++) {
      const int32_t in[2] = {in_l[i], in_r[i]};
      int32_t out[2];
      alt->frame(own, in, out);
      got[0][i] = out[0];
      got[1][i] = out[1];
    }
    break;
  }
}

/* Both instances through `frames` frames of the test signal in random buffer
 * sizes, the variant through its entry points in turn, comparing their outputs
 * when `compare` is set. */
static void alt_play(alt_run *run, size_t frames, int compare, const char *label) {
  int32_t in_l[CHUNK_MAX];
  int32_t in_r[CHUNK_MAX];
  int32_t want[2][CHUNK_MAX];
  int32_t got[2][CHUNK_MAX];
  while (frames != 0u) {
    size_t count = 1u + next_random(&run->seed) % CHUNK_MAX;
    if (count > frames) {
      count = frames;
    }
    gm2_signal_fill(&run->signal, in_l, in_r, count);
    run->effect->split(run->exact, in_l, in_r, want[0], want[1], count);
    alt_entry(run->alt, run->own, run->entry++ % ALT_ENTRY_POINTS, in_l, in_r, got, count);
    frames -= count;
    if (!compare) {
      for (size_t i = 0; i < count; i++) {
        run->departed += got[0][i] != want[0][i] || got[1][i] != want[1][i];
      }
      continue;
    }
    if (run->broken) {
      continue;
    }
    for (size_t i = 0; i < count; i++) {
      for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
        if (got[channel][i] != want[channel][i]) {
          detail("%s after %s: channel %u is %06X, the effect gives %06X", run->alt->name, label,
                 channel, (uint32_t)got[channel][i] & 0xFFFFFFu,
                 (uint32_t)want[channel][i] & 0xFFFFFFu);
          failed();
          run->broken = 1;
          return;
        }
      }
    }
    run->compared += count;
  }
}

static void alt_turn(alt_run *run, unsigned knob, int value) {
  if (run->effect->set_knob(run->exact, knob, value) != 0 ||
      run->alt->set_knob(run->own, knob, value) != 0) {
    detail("%s: %s = %d has no setup", run->alt->name, run->alt->knob[knob].name, value);
    failed();
    run->broken = 1;
  }
}

/* The settle after a turn, then the frames that must match. */
static void alt_hold(alt_run *run, unsigned knob, const char *what) {
  char label[96];
  snprintf(label, sizeof label, "%.24s %s", run->alt->knob[knob].name, what);
  alt_play(run, ALT_SETTLE_FRAMES, 0, label);
  alt_play(run, ALT_COMPARE_FRAMES, 1, label);
}

static int alt_share(const gm2_knob *record, double share) {
  return record->min + (int)lround(share * (record->max - record->min));
}

/* A variant against its effect, from the defaults: both must give the same
 * output before any turn and again once each turn has settled. Every knob in
 * turn jumps to its minimum and its maximum, sweeps back down in steps, takes
 * fast turns between 33 % and 66 % and goes back to its default. */
static unsigned check_alt(const gm2_effect *effect, const gm2_effect *alt) {
  item_failures = 0;
  item_shown = 0;
  unsigned turns = 0;
  int32_t values[64];
  alt_run run = {.effect = effect, .alt = alt, .seed = 0x5EEDu};
  if (effect->knobs != alt->knobs || effect->knobs > sizeof values / sizeof values[0]) {
    printf("%-17s FAIL  its knob table is not %s's\n", alt->name, effect->name);
    return 1;
  }
  check_range(alt);
  if (alt->rejects() != 0) {
    detail("the setup accepts a NULL pointer or a context without a rate");
    failed();
  }
  gm2_effect_defaults(alt, values);
  run.exact = effect->open(values);
  run.own = alt->open(values);
  if (run.exact == NULL || run.own == NULL) {
    detail("no instance at the defaults");
    failed();
  } else {
    gm2_signal_init(&run.signal);
    alt_play(&run, ALT_COMPARE_FRAMES, 1, "the start");
    for (unsigned knob = 0; knob < alt->knobs && !run.broken; knob++) {
      const gm2_knob *record = &alt->knob[knob];
      alt_turn(&run, knob, record->min);
      alt_hold(&run, knob, "to its minimum");
      alt_turn(&run, knob, record->max);
      alt_hold(&run, knob, "to its maximum");
      turns += 2u;
      for (unsigned step = 1; step <= ALT_SWEEP_STEPS; step++) {
        alt_turn(&run, knob, alt_share(record, 1.0 - (double)step / ALT_SWEEP_STEPS));
        alt_play(&run, ALT_SWEEP_FRAMES + step % GM2_CHUNK_FRAMES, 0, "");
      }
      alt_hold(&run, knob, "swept down");
      turns += ALT_SWEEP_STEPS;
      for (unsigned turn = 0; turn < ALT_FAST_TURNS; turn++) {
        alt_turn(&run, knob, alt_share(record, turn % 2u == 0u ? 0.33 : 0.66));
        alt_play(&run, ALT_FAST_FRAMES + turn % GM2_CHUNK_FRAMES, 0, "");
      }
      alt_hold(&run, knob, "after fast turns");
      turns += ALT_FAST_TURNS;
      alt_turn(&run, knob, record->def);
      alt_hold(&run, knob, "back to its default");
      turns++;
    }
  }
  if (!run.broken && run.departed == 0u) {
    detail("%s never played anything but %s's output", alt->name, effect->name);
    failed();
  }
  if (run.exact != NULL) {
    effect->close(run.exact);
  }
  if (run.own != NULL) {
    alt->close(run.own);
  }
  int ok = item_failures == 0;
  printf("%-17s %s  %u turns, %zu settled frames equal to %s's (split, float, interleaved "
         "and frame), %zu frames of its own, "
         "descriptor windows, refusals\n",
         alt->name, ok ? "PASS" : "FAIL", turns, run.compared, effect->name, run.departed);
  return ok ? 0 : 1;
}

/* --- the host-facing additions the firmware has no answer for ------------- */

static void expect(int ok, const char *what) {
  if (!ok) {
    detail("%s", what);
    failed();
  }
}

static int same_swf(gm2_swf a, gm2_swf b) { return a.m == b.m && a.e == b.e; }

/* The fixtures prove the buffers hold the longest delays only at the recorded rate. */
_Static_assert(GM2_SAMPLE_RATE_MAX == GM2_EFFECT_RATE,
               "the largest accepted rate must be the recorded rate");

/* gm2_context_init and gm2_context_rate: a rate outside 1..GM2_SAMPLE_RATE_MAX
 * leaves no rate. */
static unsigned check_context(void) {
  gm2_context context = {GM2_EFFECT_RATE};
  expect(gm2_context_init(NULL, GM2_EFFECT_RATE) == -1, "a NULL context is accepted");
  expect(gm2_context_init(&context, 0u) == -1 && gm2_context_rate(&context) == 0u,
         "a rate of 0 is accepted");
  expect(gm2_context_init(&context, GM2_SAMPLE_RATE_MAX) == 0 &&
             gm2_context_rate(&context) == GM2_SAMPLE_RATE_MAX,
         "the largest rate is refused");
  expect(gm2_context_init(&context, GM2_SAMPLE_RATE_MAX + 1u) == -1 &&
             gm2_context_rate(&context) == 0u,
         "a rate above the largest is accepted");
  gm2_context eq_context;
  expect(gm2_context_init(&eq_context, 1000u) == 0, "the EQ probe rate is refused");
  int eq_rejects = 1;
  for (unsigned band = 0; band < GM2_EQ_BANDS; band++) {
    gm2_eq_setup_raw eq_raw = {0};
    gm2_eq_config eq_config;
    eq_raw.band[band].freq = 120;
    eq_raw.band[band].gain = 6;
    eq_raw.band[band].width = 10;
    if (gm2_eq_setup_config(&eq_context, &eq_raw, &eq_config) != -1) {
      eq_rejects = 0;
    }
  }
  expect(eq_rejects, "the EQ accepts a band above half the sample rate");
  return 5;
}

/* The beat rule every tempo chain runs its beat through: a NaN takes the factory
 * Tap Time, every other length is forced into the Tap Time's window, a whole
 * millisecond is the integer the device holds, and any other length is the
 * nearest value on the mantissa grid. */
static unsigned check_beat(void) {
  const gm2_swf lowest = gm2_swf_from_int((int32_t)GM2_LFO_BEAT_MS_MIN);
  const gm2_swf highest = gm2_swf_from_int((int32_t)GM2_LFO_BEAT_MS_MAX);
  unsigned compared = 0;
  for (int32_t ms = (int32_t)GM2_LFO_BEAT_MS_MIN; ms <= (int32_t)GM2_LFO_BEAT_MS_MAX; ms++) {
    compared++;
    if (!same_swf(gm2_lfo_beat((double)ms), gm2_swf_from_int(ms))) {
      detail("a beat of %d ms is not the firmware's integer", (int)ms);
      failed();
    }
  }
  expect(same_swf(gm2_lfo_beat((double)NAN), gm2_swf_from_int((int32_t)GM2_LFO_BEAT_MS_DEFAULT)),
         "a NaN beat is not the factory Tap Time");
  expect(same_swf(gm2_lfo_beat(GM2_LFO_BEAT_MS_MIN - 0.5), lowest) &&
             same_swf(gm2_lfo_beat(-(double)INFINITY), lowest),
         "a beat below the window is not forced into it");
  expect(same_swf(gm2_lfo_beat(GM2_LFO_BEAT_MS_MAX + 0.5), highest) &&
             same_swf(gm2_lfo_beat((double)INFINITY), highest),
         "a beat above the window is not forced into it");
  expect(same_swf(gm2_lfo_beat(250.5), gm2_swf_div(gm2_swf_from_int(501), gm2_swf_two)),
         "a beat of 250.5 ms is not half of 501 ms");
  expect(same_swf(gm2_lfo_beat(127.99999999), gm2_swf_from_int(128)),
         "a beat just below 128 ms does not round up to it");
  for (unsigned note = 1; note <= GM2_LFO_NOTE_MAX; note++) {
    const double low = GM2_LFO_BEAT_MS_MIN - 1.0;
    const double high = GM2_LFO_BEAT_MS_MAX + 1.0;
    expect(same_swf(gm2_lfo_rate(0, note, (double)NAN),
                    gm2_lfo_rate(0, note, GM2_LFO_BEAT_MS_DEFAULT)) &&
               gm2_delay_design_ms(0, 0, 1800, (int)note, (double)NAN) ==
                   gm2_delay_design_ms(0, 0, 1800, (int)note, GM2_LFO_BEAT_MS_DEFAULT),
           "a NaN beat is not the factory Tap Time");
    expect(same_swf(gm2_lfo_rate(0, note, low), gm2_lfo_rate(0, note, GM2_LFO_BEAT_MS_MIN)) &&
               gm2_delay_design_ms(0, 0, 1800, (int)note, low) ==
                   gm2_delay_design_ms(0, 0, 1800, (int)note, GM2_LFO_BEAT_MS_MIN),
           "a beat below the window is not forced into it");
    expect(same_swf(gm2_lfo_rate(0, note, high), gm2_lfo_rate(0, note, GM2_LFO_BEAT_MS_MAX)) &&
               gm2_delay_design_ms(0, 0, 1800, (int)note, high) ==
                   gm2_delay_design_ms(0, 0, 1800, (int)note, GM2_LFO_BEAT_MS_MAX),
           "a beat above the window is not forced into it");
  }
  return compared + 5u;
}

/* gm2_lfo_phase_at against its definition: a cycle is one note value long and
 * starts on beat 0. A quarter of the note's beats is a quarter of its divisor in
 * cycles. */
static unsigned check_phase(void) {
  unsigned compared = 0;
  for (unsigned note = 1; note <= GM2_LFO_NOTE_MAX; note++) {
    const double beats = (double)gm2_note_beats[note];
    const int32_t quarter = (int32_t)((gm2_note_divisor[note] % 4) * 0x200000);
    compared += 4u;
    if (gm2_lfo_phase_at(0.0, note) != 0 || gm2_lfo_phase_at(3.0 * beats, note) != 0 ||
        gm2_lfo_phase_at(beats / 4.0, note) != quarter ||
        gm2_lfo_phase_at(-beats / 4.0, note) != ((0x800000 - quarter) & 0x7FFFFF)) {
      detail("note %u: the phase is not the position's fraction of a cycle", note);
      failed();
    }
  }
  expect(gm2_lfo_phase_at((double)NAN, 1) == 0 && gm2_lfo_phase_at((double)INFINITY, 1) == 0 &&
             gm2_lfo_phase_at(1e300, 1) == 0,
         "a position without a fraction is not a cycle boundary");
  return compared + 1u;
}

#define ALIGN_FRAMES 4096u
/* The Tempo value the align runs use, a dotted quarter: a cycle of 1.5 beats. */
#define ALIGN_NOTE 5
#define ALIGN_CYCLE 1.5

/* One run of ALIGN_FRAMES frames from a fresh instance, aligned to `beats` first
 * unless that is NaN. 0 on success. */
static int align_run(const gm2_effect *effect, const int32_t *values, double beats,
                     const int32_t *left, const int32_t *right, int32_t *out) {
  void *instance = effect->open(values);
  if (instance == NULL) {
    return -1;
  }
  if (beats == beats) {
    effect->align(instance, beats);
  }
  effect->split(instance, left, right, out, out + ALIGN_FRAMES, ALIGN_FRAMES);
  effect->close(instance);
  return 0;
}

/* An effect's align call: a position on a cycle boundary is the rest state's, a
 * position inside a cycle moves the LFO, and with Tempo at Ignore the LFO is left
 * alone. */
static unsigned check_align(const gm2_effect *effect) {
  enum { RUNS = 6 };
  static int32_t left[ALIGN_FRAMES];
  static int32_t right[ALIGN_FRAMES];
  static int32_t out[RUNS][2u * ALIGN_FRAMES];
  int32_t *values = malloc(effect->knobs * sizeof *values);
  int32_t *ignore = malloc(effect->knobs * sizeof *ignore);
  if (values == NULL || ignore == NULL) {
    detail("out of memory");
    failed();
    free(values);
    free(ignore);
    return 0;
  }
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    const int tempo = strstr(effect->knob[knob].name, "Tempo") != NULL;
    values[knob] = tempo ? ALIGN_NOTE : effect->knob[knob].def;
    ignore[knob] = tempo ? 0 : effect->knob[knob].def;
  }
  gm2_signal signal;
  gm2_signal_init(&signal);
  gm2_signal_fill(&signal, left, right, ALIGN_FRAMES);
  const int ran = align_run(effect, values, (double)NAN, left, right, out[0]) == 0 &&
                  align_run(effect, values, 0.0, left, right, out[1]) == 0 &&
                  align_run(effect, values, 2.0 * ALIGN_CYCLE, left, right, out[2]) == 0 &&
                  align_run(effect, values, ALIGN_CYCLE / 4.0, left, right, out[3]) == 0 &&
                  align_run(effect, ignore, (double)NAN, left, right, out[4]) == 0 &&
                  align_run(effect, ignore, ALIGN_CYCLE / 4.0, left, right, out[5]) == 0;
  free(values);
  free(ignore);
  if (!ran) {
    detail("%s: the effect refused a setting", effect->name);
    failed();
    return 0;
  }
  if (memcmp(out[1], out[0], sizeof out[0]) != 0 || memcmp(out[2], out[0], sizeof out[0]) != 0) {
    detail("%s: a cycle boundary is not the rest state's LFO position", effect->name);
    failed();
  }
  if (memcmp(out[3], out[0], sizeof out[0]) == 0) {
    detail("%s: a quarter of a cycle leaves the LFO where it was", effect->name);
    failed();
  }
  if (memcmp(out[5], out[4], sizeof out[4]) != 0) {
    detail("%s: align moves the LFO with Tempo at Ignore", effect->name);
    failed();
  }
  return 1;
}

/* The firmware's software float against the port: same mantissa, same exponent. */
static unsigned check_swf(void) {
  gm2_fixture fixture;
  item_failures = 0;
  item_shown = 0;
  if (gm2_fixture_load(&fixture, FIXTURE_DIR "/swf.bin") != 0) {
    printf("%-17s FAIL  cannot load " FIXTURE_DIR "/swf.bin\n", "swf");
    return 1;
  }
  uint32_t count = 0;
  uint32_t size = 0;
  const void *records = gm2_fixture_records(&fixture, GM2_FIX_SWF, &count, &size);
  unsigned compared = 0;
  if (records == NULL || size != GM2_FIX_SWF_SIZE) {
    detail("the fixture holds no software-float records");
    failed();
  }
  for (uint32_t index = 0; records != NULL && index < count; index++) {
    gm2_fixture_swf entry;
    gm2_fixture_get_swf((const unsigned char *)records + (size_t)size * index, &entry);
    const gm2_swf a = {entry.a_m, entry.a_e};
    const gm2_swf b = {entry.b_m, entry.b_e};
    const gm2_swf c = {entry.c_m, entry.c_e};
    gm2_swf got = {0, 0};
    switch (entry.op) {
    case GM2_SWF_ADD:
      got = gm2_swf_add(a, b);
      break;
    case GM2_SWF_SUB: /* P:0x000B5F subtracts its accumulator operand from its X operand */
      got = gm2_swf_sub(b, a);
      break;
    case GM2_SWF_MUL:
      got = gm2_swf_mul(a, b);
      break;
    case GM2_SWF_DIV:
      got = gm2_swf_div(a, b);
      break;
    case GM2_SWF_NEG:
      got = gm2_swf_neg(a);
      break;
    case GM2_SWF_CMP:
      got = (gm2_swf){gm2_swf_cmp(a, b), 0};
      break;
    case GM2_SWF_MAC:
      got = gm2_swf_mac(a, b, c);
      break;
    case GM2_SWF_MSUB:
      got = gm2_swf_msub(a, b, c);
      break;
    case GM2_SWF_FROM_INT:
      got = gm2_swf_from_int(a.m);
      break;
    case GM2_SWF_TO_INT:
      got = (gm2_swf){gm2_swf_to_int(a), 0};
      break;
    case GM2_SWF_ABS:
      got = gm2_swf_abs(a);
      break;
    case GM2_SWF_EXP:
      got = gm2_swf_exp(a);
      break;
    case GM2_SWF_LN:
      got = gm2_swf_ln(a);
      break;
    case GM2_SWF_POW:
      got = gm2_swf_pow(a, b);
      break;
    case GM2_SWF_SQRT:
      got = gm2_swf_sqrt(a);
      break;
    case GM2_SWF_SINH:
      got = gm2_swf_sinh(a);
      break;
    case GM2_SWF_ATAN:
      got = gm2_swf_atan(a);
      break;
    case GM2_SWF_TAN:
      got = gm2_swf_tan(a);
      break;
    case GM2_SWF_SIN:
      got = gm2_swf_sin(a);
      break;
    case GM2_SWF_COS:
      got = gm2_swf_cos(a);
      break;
    case GM2_SWF_HZ20:
      got = gm2_swf_hz20(a.m);
      break;
    case GM2_SWF_HZ80:
      got = gm2_swf_hz80(a.m);
      break;
    case GM2_SWF_DB:
      got = gm2_swf_db(a.m);
      break;
    case GM2_SWF_DB_TO_LIN:
      got = (gm2_swf){gm2_swf_db_to_lin(a.m), 0};
      break;
    case GM2_SWF_TO_INT48: {
      int32_t low = 0;
      int32_t high = 0;
      gm2_fixture_split48(gm2_swf_to_int48(a), &low, &high);
      got = (gm2_swf){low, high};
      break;
    }
    case GM2_SWF_FROM_INT48:
      got = gm2_swf_from_int48(gm2_fixture_int48(a.m, a.e));
      break;
    default:
      detail("record %u has unknown operation %u", index, entry.op);
      failed();
      continue;
    }
    compared++;
    if (got.m != entry.r_m || got.e != entry.r_e) {
      detail("%s record %u a={%06X,%3d} b={%06X,%3d} c={%06X,%3d}: port {%06X,%3d}, firmware "
             "{%06X,%3d}",
             gm2_swf_op_name(entry.op), index, (uint32_t)a.m & 0xFFFFFFu, a.e,
             (uint32_t)b.m & 0xFFFFFFu, b.e, (uint32_t)c.m & 0xFFFFFFu, c.e,
             (uint32_t)got.m & 0xFFFFFFu, got.e, (uint32_t)entry.r_m & 0xFFFFFFu, entry.r_e);
      failed();
    }
  }
  gm2_fixture_free(&fixture);
  int ok = item_failures == 0;
  printf("%-17s %s  %u operations\n", "swf", ok ? "PASS" : "FAIL", compared);
  return ok ? 0 : 1;
}

/* One lookup table the library keeps, under the name the fixture gives it: plain
 * 32-bit words, or a software-float table whose (mantissa, exponent) pairs the
 * firmware stores the other way round, exponent first. */
typedef struct {
  const char *name;
  const int32_t *words;
  const gm2_swf *values;
  uint32_t count;
} native_table;

static const native_table k_tables[] = {
    {"gm2_log2_table", gm2_log2_table, NULL, GM2_LOG2_TABLE_WORDS},
    {"gm2_exp2_table", gm2_exp2_table, NULL, GM2_EXP2_TABLE_WORDS},
    {"gm2_lfo_sine_table", gm2_lfo_sine_table, NULL, GM2_LFO_SINE_WORDS},
    {"gm2_sine_table", gm2_sine_table, NULL, GM2_LFO_SINE_WORDS},
    {"gm2_phaser_range_table", NULL, gm2_phaser_range_table, 2u * GM2_PHASER_RANGE_WORDS},
    {"gm2_univibe_ap_table", NULL, gm2_univibe_ap_table, 2u * GM2_UNIVIBE_AP_WORDS},
    {"gm2_trichorus_norm_lfo_table", &gm2_trichorus_norm_lfo_table[0][0], NULL,
     5u * GM2_LFO_SINE_WORDS},
    {"gm2_trichorus_asym_lfo_table[0]", gm2_trichorus_asym_lfo_table[0], NULL, GM2_LFO_SINE_WORDS},
    {"gm2_trichorus_asym_lfo_table[1]", gm2_trichorus_asym_lfo_table[1], NULL, GM2_LFO_SINE_WORDS},
    {"gm2_trichorus_asym_lfo_set[0]", gm2_trichorus_asym_lfo_set[0], NULL, GM2_LFO_SINE_WORDS},
    {"gm2_trichorus_asym_lfo_set[1]", gm2_trichorus_asym_lfo_set[1], NULL, GM2_LFO_SINE_WORDS},
    {"gm2_trichorus_asym_lfo_set[2]", gm2_trichorus_asym_lfo_set[2], NULL, GM2_LFO_SINE_WORDS},
    {"gm2_moddelay_pharev_table", gm2_moddelay_pharev_table, NULL, GM2_MODDELAY_PHAREV_WORDS},
    {"gm2_moddelay_voice_gains[0..2]", &gm2_moddelay_voice_gains[0][0], NULL,
     3u * GM2_MODDELAY_VOICES},
    {"gm2_moddelay_voice_gains[3]", gm2_moddelay_voice_gains[3], NULL, GM2_MODDELAY_VOICES},
    {"gm2_moddelay_voice_gains[4]", gm2_moddelay_voice_gains[4], NULL, GM2_MODDELAY_VOICES},
    {"gm2_moddelay_depth_norm_table", gm2_moddelay_depth_norm_table, NULL,
     GM2_MODDELAY_DEPTH_CHO_WORDS},
    {"gm2_moddelay_depth_asym_table", gm2_moddelay_depth_asym_table, NULL,
     GM2_MODDELAY_DEPTH_CHO_WORDS},
    {"gm2_moddelay_depth_tzf_table", gm2_moddelay_depth_tzf_table, NULL,
     GM2_MODDELAY_DEPTH_TZF_WORDS},
    {"gm2_moddelay_width_norm_table", gm2_moddelay_width_norm_table, NULL,
     GM2_MODDELAY_WIDTH_WORDS},
    {"gm2_moddelay_width_asym_table", gm2_moddelay_width_asym_table, NULL,
     GM2_MODDELAY_WIDTH_WORDS},
    {"gm2_pitch_scale_table", &gm2_pitch_scale_table[0][0], NULL,
     GM2_PITCH_SCALE_COUNT * GM2_PITCH_SCALE_STRIDE},
    {"gm2_pitch_det_biquad", gm2_pitch_det_biquad, NULL, GM2_PITCH_DET_BIQUAD_WORDS},
    {"gm2_pitch_log2_poly", gm2_pitch_log2_poly, NULL, GM2_PITCH_LOG2_POLY_WORDS},
    {"gm2_pitch_exp2_poly", gm2_pitch_exp2_poly, NULL, GM2_PITCH_EXP2_POLY_WORDS},
    {"gm2_note_beats", gm2_note_beats, NULL, GM2_NOTE_VALUES},
    {"gm2_note_divisor", gm2_note_divisor, NULL, GM2_NOTE_VALUES},
    {"gm2_delay_note_table[1..16]", NULL, gm2_delay_note_table + 1u, 2u * (GM2_NOTE_VALUES - 1u)},
    {"gm2_ratio_table", NULL, gm2_ratio_table, 2u * GM2_RATIO_TABLE_ENTRIES},
    {"gm2_time_table", NULL, gm2_time_table, 2u * GM2_TIME_TABLE_ENTRIES},
    {"gm2_biquad_q_table", NULL, gm2_biquad_q_table, 2u * GM2_BIQUAD_Q_ENTRIES},
    {"gm2_reverb_decay_table", gm2_reverb_decay_table, NULL, GM2_REVERB_DECAY_WORDS},
    {"gm2_reverb_early_l_table", gm2_reverb_early_l_table, NULL, GM2_REVERB_EARLY_WORDS},
    {"gm2_reverb_early_r_table", gm2_reverb_early_r_table, NULL, GM2_REVERB_EARLY_WORDS},
    {"gm2_reverb_size_table", gm2_reverb_size_table, NULL, GM2_REVERB_SIZE_WORDS},
    {"gm2_reverb_color_biquad_table", gm2_reverb_color_biquad_table, NULL,
     GM2_REVERB_COLOR_BIQUAD_WORDS},
    {"gm2_reverb_locol_table", gm2_reverb_locol_table, NULL, GM2_REVERB_LOCOL_WORDS},
    {"gm2_reverb_hicol_table", gm2_reverb_hicol_table, NULL, GM2_REVERB_HICOL_WORDS},
    {"gm2_reverb_tank_lengths", gm2_reverb_tank_lengths, NULL, GM2_REVERB_TANK_LENGTH_WORDS},
    {"gm2_reverb_tank_signs", gm2_reverb_tank_signs, NULL, GM2_REVERB_TANK_SIGN_WORDS},
};

#define TABLE_COUNT (sizeof k_tables / sizeof k_tables[0])

static int32_t native_word(const native_table *table, uint32_t index) {
  if (table->values == NULL) {
    return table->words[index];
  }
  const gm2_swf value = table->values[index / 2u];
  return (index & 1u) == 0u ? value.e : value.m;
}

static const native_table *find_table(const char *name) {
  for (size_t i = 0; i < TABLE_COUNT; i++) {
    if (strcmp(name, k_tables[i].name) == 0) {
      return &k_tables[i];
    }
  }
  return NULL;
}

/* Every table gm2_tables.c keeps, word for word against the firmware's. */
static unsigned check_tables(void) {
  gm2_fixture fixture;
  item_failures = 0;
  item_shown = 0;
  if (gm2_fixture_load(&fixture, FIXTURE_DIR "/tables.bin") != 0) {
    printf("%-17s FAIL  cannot load " FIXTURE_DIR "/tables.bin\n", "tables");
    return 1;
  }
  uint32_t index_count = 0;
  uint32_t index_size = 0;
  uint32_t word_count = 0;
  uint32_t word_size = 0;
  const void *index_records =
      gm2_fixture_records(&fixture, GM2_FIX_TABLE_INDEX, &index_count, &index_size);
  const void *words = gm2_fixture_records(&fixture, GM2_FIX_TABLE_WORDS, &word_count, &word_size);
  unsigned matched = 0;
  if (index_records == NULL || words == NULL || index_size != GM2_FIX_TABLE_INDEX_SIZE ||
      word_size != GM2_FIX_TABLE_WORDS_SIZE) {
    detail("the fixture holds no usable table sections");
    failed();
    goto done;
  }
  unsigned char seen[TABLE_COUNT];
  memset(seen, 0, sizeof seen);
  for (uint32_t record = 0; record < index_count; record++) {
    gm2_fixture_table index;
    gm2_fixture_get_table((const unsigned char *)index_records + (size_t)index_size * record,
                          &index);
    const native_table *table = find_table(index.name);
    if (table == NULL) {
      detail("the fixture holds %s, which the library does not", index.name);
      failed();
      continue;
    }
    if (index.count != table->count) {
      detail("%s is %u words, the library has %u", index.name, (unsigned)index.count,
             (unsigned)table->count);
      failed();
      continue;
    }
    seen[table - k_tables] = 1;
    for (uint32_t word = 0; word < index.count; word++) {
      const int32_t want = gm2_fixture_get_word(
          (const unsigned char *)words + GM2_FIX_TABLE_WORDS_SIZE * (size_t)(index.first + word));
      const int32_t got = native_word(table, word);
      if (want != got) {
        detail("%s word %u: %06X, firmware %06X", index.name, (unsigned)word,
               (uint32_t)got & 0xFFFFFFu, (uint32_t)want & 0xFFFFFFu);
        failed();
        break;
      }
    }
  }
  unsigned missing = 0;
  for (size_t i = 0; i < TABLE_COUNT; i++) {
    if (seen[i] != 0u) {
      matched++;
    } else if (missing++ == 0u) {
      detail("the fixture holds no %s", k_tables[i].name);
      failed();
    }
  }
  if (missing != 0u) {
    detail("the fixture covers %u of the %u tables", matched, (unsigned)TABLE_COUNT);
    failed();
  }

done:
  gm2_fixture_free(&fixture);
  int ok = item_failures == 0;
  printf("%-17s %s  %u tables, %u words\n", "tables", ok ? "PASS" : "FAIL", index_count,
         word_count);
  return ok ? 0 : 1;
}

static const gm2_effect *const k_effects[] = {
#define GM2_EFFECT_ENTRY(name) &gm2_effect_##name,
    GM2_EFFECTS(GM2_EFFECT_ENTRY)
#undef GM2_EFFECT_ENTRY
};

#define EFFECT_COUNT (sizeof k_effects / sizeof k_effects[0])

typedef struct {
  const gm2_effect *effect;
  const gm2_effect *alt;
} alt_pair;

static const alt_pair k_alts[] = {
#define GM2_ALT_ENTRY(name, alt) {&gm2_effect_##name, &gm2_effect_##alt},
    GM2_ALT_EFFECTS(GM2_ALT_ENTRY)
#undef GM2_ALT_ENTRY
};

#define ALT_COUNT (sizeof k_alts / sizeof k_alts[0])

/* The host item: the beat rule, the song-position phase, every effect's align
 * call and the context's refusals. */
static unsigned check_host(void) {
  item_failures = 0;
  item_shown = 0;
  unsigned compared = check_context() + check_beat() + check_phase();
  unsigned aligned = 0;
  for (size_t i = 0; i < EFFECT_COUNT; i++) {
    if (k_effects[i]->align != NULL) {
      aligned += check_align(k_effects[i]);
    }
  }
  int ok = item_failures == 0;
  printf("%-17s %s  %u beat, phase and context values, %u effects aligned\n", "host",
         ok ? "PASS" : "FAIL", compared, aligned);
  return ok ? 0 : 1;
}

static void usage(const char *program) {
  fprintf(stderr, "usage: %s [name ...]\n  names:", program);
  for (size_t i = 0; i < EFFECT_COUNT; i++) {
    fprintf(stderr, " %s", k_effects[i]->name);
  }
  for (size_t i = 0; i < ALT_COUNT; i++) {
    fprintf(stderr, " %s", k_alts[i].alt->name);
  }
  fprintf(stderr, " swf tables host\n");
  fprintf(stderr, "  no name: all of them\n");
}

int main(int argc, char **argv) {
  unsigned failures = 0;
  unsigned selected[EFFECT_COUNT];
  unsigned alt_selected[ALT_COUNT];
  int swf = argc == 1;
  int tables = argc == 1;
  int host = argc == 1;
  for (size_t i = 0; i < EFFECT_COUNT; i++) {
    selected[i] = argc == 1 ? 1u : 0u;
  }
  for (size_t i = 0; i < ALT_COUNT; i++) {
    alt_selected[i] = argc == 1 ? 1u : 0u;
  }
  for (int arg = 1; arg < argc; arg++) {
    if (strcmp(argv[arg], "swf") == 0) {
      swf = 1;
      continue;
    }
    if (strcmp(argv[arg], "tables") == 0) {
      tables = 1;
      continue;
    }
    if (strcmp(argv[arg], "host") == 0) {
      host = 1;
      continue;
    }
    size_t alt = 0;
    while (alt < ALT_COUNT && strcmp(argv[arg], k_alts[alt].alt->name) != 0) {
      alt++;
    }
    if (alt < ALT_COUNT) {
      alt_selected[alt] = 1;
      continue;
    }
    size_t match = 0;
    while (match < EFFECT_COUNT && strcmp(argv[arg], k_effects[match]->name) != 0) {
      match++;
    }
    if (match == EFFECT_COUNT) {
      usage(argv[0]);
      return 2;
    }
    selected[match] = 1;
  }
  for (size_t i = 0; i < EFFECT_COUNT; i++) {
    if (selected[i] != 0u) {
      failures += check_effect(k_effects[i]);
    }
  }
  for (size_t i = 0; i < ALT_COUNT; i++) {
    if (alt_selected[i] != 0u) {
      failures += check_alt(k_alts[i].effect, k_alts[i].alt);
    }
  }
  if (swf != 0) {
    failures += check_swf();
  }
  if (tables != 0) {
    failures += check_tables();
  }
  if (host != 0) {
    failures += check_host();
  }
  return failures == 0 ? 0 : 1;
}
