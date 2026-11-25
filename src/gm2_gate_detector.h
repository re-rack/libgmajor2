/* The Noise Gate's level detector, P:0x000C4C..0x000C66 and P:0x000C78: the input
 * gain, the larger channel magnitude, an instant-attack one-pole envelope, and on
 * the first frame of each chunk the log2 of that envelope into a peak hold.
 *
 * The gate runs its first two steps and continues from the envelope itself. The
 * Touch Res Filter and the Dynamic Delay run the whole detector on their own
 * input: on the device their control tick reads the gate's peak hold
 * (P:0x090494).
 *
 * Every routine that runs the detector runs those two steps once per frame, so
 * they are inline here, as the biquad step is.
 */
#ifndef GM2_GATE_DETECTOR_H
#define GM2_GATE_DETECTOR_H

#include <stdint.h>

#include "gm2_base.h"
#include "gm2_dsp_math.h"
#include "gm2_process.h"
#include "gm2_tick.h"

/* Y:0x373, which P:0x090441 writes: the envelope's one-pole decay. */
#define GM2_GATE_DETECTOR_DECAY 0x7FDD98

typedef struct {
  int32_t envelope; /* X:0x2A2: the one-pole's state */
  int32_t peak;     /* Y:0x377: the peak hold, a log2 level */
} gm2_gate_detector;

/* An empty detector: no envelope and the peak hold at its floor, 0x800000. */
void gm2_gate_detector_init(gm2_gate_detector *detector);

/* P:0x000C4C..0x000C4E: the block's input gain, one MPYR per channel. The input
 * word sits inside the rails and the gain word just below them, so the product's
 * top word is at most 0x7FFFFE in magnitude and the store never limits. */
static inline void gm2_gate_detector_gain(int32_t gained[GM2_CHANNELS],
                                          const int32_t in[GM2_CHANNELS]) {
  int64_t a = gm2_mpyr(GM2_LEVEL_INPUT, gm2_s24((uint32_t)in[0]));
  int64_t b = gm2_mpyr(gm2_s24((uint32_t)in[1]), GM2_LEVEL_INPUT);
  gained[0] = (int32_t)gm2_floor_step24(a);
  gained[1] = (int32_t)gm2_floor_step24(b);
}

/* P:0x000C5E..0x000C66: the envelope takes the larger channel magnitude at once
 * and decays towards a smaller one. Returns its accumulator, before the 24-bit
 * store, which the log2 step reads. Its chains are one doubled product of two
 * 24-bit words plus the held envelope, so nothing reaches 2^48 and no 56-bit
 * accumulator wrap is needed. */
static inline int64_t gm2_gate_detector_follow(int32_t *envelope,
                                               const int32_t gained[GM2_CHANNELS]) {
  /* Both words are inside the rails, so their magnitudes fit an int and the
   * accumulator comparison is the same one made on the words. */
  int32_t magnitude = gained[0] < 0 ? -gained[0] : gained[0];
  const int32_t other = gained[1] < 0 ? -gained[1] : gained[1];
  if (magnitude < other) {
    magnitude = other;
  }
  int64_t detected = gm2_word_acc(magnitude);
  int64_t held = gm2_word_acc(*envelope);
  /* The envelope starts at zero and each step moves it part of the way to a
   * magnitude inside the rails, so it stays inside them: the gap to it and the
   * stepped envelope both fit a 24-bit word and neither store limits. */
  int64_t excess = detected - held;
  int64_t follow =
      excess < 0 ? 2 * (int64_t)GM2_GATE_DETECTOR_DECAY * gm2_floor_step24(excess) : excess;
  int64_t updated = follow + held;
  *envelope = (int32_t)gm2_floor_step24(updated);
  return updated;
}

/* One frame of the whole detector at position `chunk_pos` of its chunk: the peak
 * hold takes the envelope's log2 on the first frame, as P:0x000C78 does. A tick
 * reads the hold with gm2_tick_level(). */
static inline void gm2_gate_detector_frame(gm2_gate_detector *detector,
                                           const int32_t in[GM2_CHANNELS], unsigned chunk_pos) {
  int32_t gained[GM2_CHANNELS];
  gm2_gate_detector_gain(gained, in);
  const int64_t envelope = gm2_gate_detector_follow(&detector->envelope, gained);
  if (chunk_pos == 0u) {
    gm2_tick_hold(&detector->peak, envelope);
  }
}

#endif /* GM2_GATE_DETECTOR_H */
