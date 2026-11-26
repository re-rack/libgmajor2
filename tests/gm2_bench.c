/* The benchmark: runs each effect's audio kernel on Q23 test-signal buffers and
 * reports the median processing time of seven 2 s runs in nanoseconds per stereo
 * frame. Setup, input generation and conversion stay outside the timed region. */
#define _POSIX_C_SOURCE 200809L

#include "gm2_effect.h"
#include "gm2_signal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
  BENCH_BUFFER = 128,
  BENCH_SIGNAL_FRAMES = 65536,
  BENCH_WARMUP_FRAMES = 48000,
  BENCH_RUN_FRAMES = 96000,
  BENCH_RUNS = 7,
};

typedef struct {
  const gm2_effect *effect;
  const int32_t *values;
  size_t count;
} bench_setting;

static const int32_t k_eq[] = {60, 6, 10, 100, -6, 6, 180, 3, 4};
static const int32_t k_gate[] = {0, -20, 90, 45};
static const int32_t k_wahwah[] = {50, 0, 66, 0};
static const int32_t k_touchwah[] = {0, 2, 0, 66, 0};
/* Shape 0 runs the triangle, the heavier waveform; Tempo at Ignore runs Speed. */
static const int32_t k_autowah[] = {208, 0, 0, 0, 66, 0};
static const int32_t k_resfilter[] = {1, 57, 100, 66, 0};
static const int32_t k_touchres[] = {1, 0, 2, 54, 66, 0};
/* Tempo at Ignore; Speed max still crosses both waveform branches. */
static const int32_t k_tremolo[] = {208, 100, 0, 0, 33, 58, 0};
static const int32_t k_panner[] = {116, 100, 0, 0};
/* Threshold at its minimum holds the gain computer on the attack side. */
static const int32_t k_comp[] = {-40, 11, 3, 15, 0};
static const int32_t k_detune[] = {9, -9, 50, 50, 66, 0};
static const int32_t k_whammy[] = {33, 0, 2, 0};
static const int32_t k_octaver[] = {0, 2, 66, 0};
/* Voice 1 at its minimum exercises the negative-ratio wrap. */
static const int32_t k_pitch[] = {-2400, -15, -50, 50, 350, 350, 100, 100, 0, 0, 66, 0};
static const int32_t k_ipitch[] = {10, -12, -50, 50, 350, 350, 0, 0, 0, 0, 66, 0};
static const int32_t k_cchorus[] = {69, 50, 0, 58, 50, 0};
static const int32_t k_achorus[] = {69, 50, 0, 58, 500, 1, 1, 50, 0};
static const int32_t k_trichorus[] = {69, 50, 0, 50, 1, 50, 0};
static const int32_t k_trichorus_asym[] = {69, 50, 0, 50, 1, 50, 0};
static const int32_t k_cflanger[] = {69, 25, 0, 58, -80, 54, 50, 0};
static const int32_t k_aflanger[] = {69, 20, 0, 58, -80, 54, 500, 1, 1, 50, 0};
static const int32_t k_tzflanger_neg[] = {69, 20, 0, -80, 45, 0};
static const int32_t k_tzflanger_pos[] = {69, 20, 0, -80, 45, 0};
static const int32_t k_vibrato[] = {152, 7, 0, 58, 0};
static const int32_t k_vintphaser[] = {69, 90, 0, -90, 0, 0, 66, 0};
static const int32_t k_smoothphaser[] = {69, 80, 0, -90, 0, 1, 66, 0};
static const int32_t k_vintunivibe[] = {128, 37, 90, 0, 0, 1, 1, 0, 0, 66, 0};
static const int32_t k_modunivibe[] = {128, 37, 100, 0, 0, 1, 1, 0, 0, 66, 0};
static const int32_t k_pingpong[] = {375, 0, 75, 100, 54, 10, 66, 0};
static const int32_t k_dyndelay[] = {1800, 0, 100, 55, 8, 25, -25, 18, 14, 66, 0};
static const int32_t k_dualdelay[] = {280, 420, 0, 0, 100, 100, 53, 12, -30, 35, 66, 0};
static const int32_t k_revdelay[] = {1000, 0, 100, 54, 10, 66, 0};
/* Each Decay is high enough to reach the 24-bit store's rail branch. */
static const int32_t k_reverb_spring[] = {67, 0, 0, 3, 4, -8, 3, 0, 0, -3, 10, 66, 0};
static const int32_t k_reverb_hall[] = {134, 50, 0, 4, 3, 0, 4, 9, 0, -6, 0, 66, 0};
static const int32_t k_reverb_room[] = {67, 33, 0, 3, 2, 0, 4, 9, 0, -6, 0, 66, 0};
static const int32_t k_reverb_plate[] = {134, 5, 1, 7, 1, 5, 5, 6, 0, -6, 10, 66, 0};

static const bench_setting k_settings[] = {
#define GM2_BENCH_ENTRY(name) {&gm2_effect_##name, k_##name, sizeof k_##name / sizeof k_##name[0]},
    GM2_EFFECTS(GM2_BENCH_ENTRY)
#undef GM2_BENCH_ENTRY
/* A de-popped variant runs at its effect's setting, with no turn: the cost it adds
 * at rest. */
#define GM2_BENCH_ALT_ENTRY(name, alt)                                                             \
  {&gm2_effect_##alt, k_##name, sizeof k_##name / sizeof k_##name[0]},
        GM2_ALT_EFFECTS(GM2_BENCH_ALT_ENTRY)
#undef GM2_BENCH_ALT_ENTRY
};

static const bench_setting *bench_find(const char *name) {
  for (size_t i = 0; i < sizeof k_settings / sizeof k_settings[0]; i++) {
    if (strcmp(k_settings[i].effect->name, name) == 0) {
      return &k_settings[i];
    }
  }
  return NULL;
}

static int bench_validate(void) {
  int ok = 1;
  for (size_t i = 0; i < sizeof k_settings / sizeof k_settings[0]; i++) {
    const gm2_effect *effect = k_settings[i].effect;
    if (k_settings[i].count != effect->knobs) {
      fprintf(stderr, "%s: %zu values for %u knobs\n", effect->name, k_settings[i].count,
              effect->knobs);
      ok = 0;
    }
    for (unsigned knob = 0; knob < effect->knobs && knob < k_settings[i].count; knob++) {
      const gm2_knob *record = &effect->knob[knob];
      const int32_t value = k_settings[i].values[knob];
      if (value < record->min || value > record->max) {
        fprintf(stderr, "%s: %s = %d outside %d..%d\n", effect->name, record->name, (int)value,
                record->min, record->max);
        ok = 0;
      }
    }
  }
  return ok ? 0 : -1;
}

static int64_t bench_now_ns(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;
}

static void bench_run(void *instance, const gm2_effect *effect, const int32_t *in_l,
                      const int32_t *in_r, int32_t *out_l, int32_t *out_r, size_t frames,
                      size_t *at) {
  while (frames != 0u) {
    const size_t chunk = frames < BENCH_BUFFER ? frames : BENCH_BUFFER;
    effect->split(instance, in_l + *at, in_r + *at, out_l, out_r, chunk);
    *at = (*at + chunk) % (size_t)BENCH_SIGNAL_FRAMES;
    frames -= chunk;
  }
}

static int bench_compare(const void *left, const void *right) {
  const double a = *(const double *)left;
  const double b = *(const double *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static double bench_measure(const gm2_effect *effect, const int32_t *values, const int32_t *in_l,
                            const int32_t *in_r) {
  void *instance = effect->open(values);
  if (instance == NULL) {
    return -1.0;
  }
  int32_t out_l[BENCH_BUFFER];
  int32_t out_r[BENCH_BUFFER];
  size_t at = 0;
  bench_run(instance, effect, in_l, in_r, out_l, out_r, BENCH_WARMUP_FRAMES, &at);
  double times[BENCH_RUNS];
  for (unsigned run = 0; run < BENCH_RUNS; run++) {
    const int64_t start = bench_now_ns();
    bench_run(instance, effect, in_l, in_r, out_l, out_r, BENCH_RUN_FRAMES, &at);
    const int64_t stop = bench_now_ns();
    times[run] = (double)(stop - start) / (double)BENCH_RUN_FRAMES;
  }
  effect->close(instance);
  qsort(times, BENCH_RUNS, sizeof times[0], bench_compare);
  return times[BENCH_RUNS / 2u];
}

int main(int argc, char **argv) {
  if (bench_validate() != 0) {
    return 2;
  }
  unsigned selected[sizeof k_settings / sizeof k_settings[0]] = {0};
  if (argc == 1) {
    for (size_t i = 0; i < sizeof selected / sizeof selected[0]; i++) {
      selected[i] = 1;
    }
  }
  for (int arg = 1; arg < argc; arg++) {
    const bench_setting *setting = bench_find(argv[arg]);
    if (setting == NULL) {
      fprintf(stderr, "unknown effect: %s\n", argv[arg]);
      return 2;
    }
    selected[(size_t)(setting - k_settings)] = 1;
  }

  int32_t *in_l = malloc(BENCH_SIGNAL_FRAMES * sizeof *in_l);
  int32_t *in_r = malloc(BENCH_SIGNAL_FRAMES * sizeof *in_r);
  if (in_l == NULL || in_r == NULL) {
    fprintf(stderr, "out of memory\n");
    free(in_l);
    free(in_r);
    return 1;
  }
  gm2_signal signal;
  gm2_signal_init(&signal);
  gm2_signal_fill(&signal, in_l, in_r, BENCH_SIGNAL_FRAMES);

  printf("%u kHz, %d-frame buffers, median of %d runs of %d frames\n", GM2_EFFECT_RATE / 1000u,
         BENCH_BUFFER, BENCH_RUNS, BENCH_RUN_FRAMES);
  for (size_t i = 0; i < sizeof k_settings / sizeof k_settings[0]; i++) {
    if (selected[i] == 0u) {
      continue;
    }
    const gm2_effect *effect = k_settings[i].effect;
    const double ns = bench_measure(effect, k_settings[i].values, in_l, in_r);
    if (ns < 0.0) {
      fprintf(stderr, "%s: cannot open\n", effect->name);
      free(in_l);
      free(in_r);
      return 1;
    }
    const double share = ns * 100.0 / (1e9 / (double)GM2_EFFECT_RATE);
    printf("%-15s %9.1f ns/frame   %6.2f %% of real time\n", effect->name, ns, share);
  }
  free(in_l);
  free(in_r);
  return 0;
}
