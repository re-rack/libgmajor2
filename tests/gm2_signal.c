#include "gm2_signal.h"

#define PATTERN_FRAMES 4096u
#define PART_FRAMES 512u
#define SWEEP_FIRST 3072u
#define SWEEP_FRAMES 512u
#define RATE 48000u
#define HALF_SCALE 0x400000

/* One cycle of `hz` as a phase step over the 2^32 range. */
static uint32_t hz_step(unsigned hz) { return (uint32_t)(((uint64_t)hz << 32) / RATE); }

/* Uniform over the whole 24-bit range. */
static int32_t noise_sample(uint32_t *state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return (int32_t)(x >> 8) - 0x800000;
}

static int32_t saw_sample(uint32_t *phase, uint32_t step) {
  int32_t value = (int32_t)(*phase >> 8) - 0x800000;
  *phase += step;
  return value / 2;
}

/* A parabola through the half cycles, within a fraction of a percent of a sine
 * and free of both libm and the host's rounding mode. */
static int32_t sine_sample(uint32_t phase) {
  uint32_t word = phase >> 8;
  int32_t t = (word & 0x800000u) != 0u ? (int32_t)(0xFFFFFFu - word) : (int32_t)word;
  int32_t bump = (int32_t)(((int64_t)t * (int64_t)(0x1000000 - t)) >> 23);
  int32_t value = (int32_t)(((int64_t)bump * HALF_SCALE) >> 23);
  return (phase & 0x80000000u) != 0u ? -value : value;
}

void gm2_signal_init(gm2_signal *signal) {
  signal->noise[0] = 0x2545F491u;
  signal->noise[1] = 0x9E3779B9u;
  signal->saw[0] = 0;
  signal->saw[1] = 0;
  signal->step[0] = hz_step(220);
  signal->step[1] = hz_step(330);
  signal->sweep = 0;
  signal->frame = 0;
}

void gm2_signal_next(gm2_signal *signal, int32_t *left, int32_t *right) {
  uint32_t cycle = signal->frame / PATTERN_FRAMES;
  uint32_t part = signal->frame & (PATTERN_FRAMES - 1u);
  signal->frame++;
  if ((cycle & 3u) == 1u) {
    unsigned hz = part < 2264u ? 110u : 300u;
    int32_t value = sine_sample(signal->sweep);
    signal->sweep += hz_step(hz);
    *left = value;
    *right = value;
    return;
  }
  if ((cycle & 3u) == 2u && part < 1024u) {
    int32_t value = sine_sample(signal->sweep);
    signal->sweep += hz_step(300u);
    *left = value;
    *right = value;
    return;
  }
  if (part < 1024u) {
    *left = noise_sample(&signal->noise[0]);
    *right = noise_sample(&signal->noise[1]);
  } else if (part < 2048u) {
    *left = saw_sample(&signal->saw[0], signal->step[0]);
    *right = saw_sample(&signal->saw[1], signal->step[1]);
  } else if (part < 2048u + PART_FRAMES) {
    uint32_t n = part - 2048u;
    *left = (n % 128u) < 64u ? noise_sample(&signal->noise[0]) / 2 : 0;
    *right = ((n + 32u) % 128u) < 64u ? noise_sample(&signal->noise[1]) / 2 : 0;
  } else if (part < 3072u) {
    *left = noise_sample(&signal->noise[0]) / 1024;
    *right = noise_sample(&signal->noise[1]) / 1024;
  } else if (part < 3584u) {
    unsigned index = part - SWEEP_FIRST;
    if (index == 0u) {
      signal->sweep = 0;
    }
    int32_t value = sine_sample(signal->sweep);
    signal->sweep += hz_step(20u + ((20000u - 20u) * index) / (SWEEP_FRAMES - 1u));
    *left = value;
    *right = -value;
  } else {
    uint32_t n = part - 3584u;
    int32_t value = n == 0u ? 0x7FFFFF : n == 1u ? -0x800000 : 0;
    *left = value;
    *right = value;
  }
}

void gm2_signal_fill(gm2_signal *signal, int32_t *left, int32_t *right, size_t frames) {
  for (size_t i = 0; i < frames; i++) {
    gm2_signal_next(signal, left + i, right + i);
  }
}
