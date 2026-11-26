/* The stereo test pattern every fixture audio run is driven with: 4,096 frames
 * that repeat, with the noise continuing from one repeat to the next.
 *
 *   0-1023    full-scale noise
 *   1024-2047 sawtooth 220 Hz left and 330 Hz right, -6 dBFS
 *   2048-2559 noise bursts at -6 dBFS, 64 frames on and 64 off, right shifted 32
 *   2560-3071 noise at -60 dBFS
 *   3072-3583 sweep from 20 Hz to 20 kHz, -6 dBFS, right is the left negated
 *   3584-4095 0x7FFFFF, -0x800000, then silence on both channels
 *
 * A four-cycle pitch transition window overrides the pattern: cycle 1 is a clean
 * monophonic note of 110 Hz that changes to 300 Hz at frame 2264, and cycle 2
 * continues 300 Hz for its first 1024 frames. Cycles 0 and 3 run the pattern
 * above.
 *
 * Integer only, so x86-64 and AArch64 generate the same samples.
 */
#ifndef GM2_SIGNAL_H
#define GM2_SIGNAL_H

#include <stddef.h>
#include <stdint.h>

/* One step of the xorshift generator behind the noise and the recorder's random
 * operands. */
static inline uint32_t gm2_signal_xorshift(uint32_t *state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

typedef struct {
  uint32_t noise[2];
  uint32_t saw[2];
  uint32_t step[2];
  uint32_t sweep;
  uint32_t frame;
} gm2_signal;

void gm2_signal_init(gm2_signal *signal);

/* One stereo frame of signed 24-bit samples. */
void gm2_signal_next(gm2_signal *signal, int32_t *left, int32_t *right);

void gm2_signal_fill(gm2_signal *signal, int32_t *left, int32_t *right, size_t frames);

#endif /* GM2_SIGNAL_H */
