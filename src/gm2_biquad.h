/* The double-precision biquad of the EQ (P:0x000CCD), the resonance filters
 * (P:0x000EFB) and the chorus routine's output stage (in P:0x001117).
 *
 * A stage keeps its last output as a 48-bit accumulator and the output before
 * that as a 24-bit word. Its five coefficient words enter as
 *
 *   y[n] = y[n-1] + 8 (c0 x[n-1] + c1 x[n-2] + c2 x[n] + c3 hi(y[n-1])) + 4 c4 y[n-2]
 *
 * with hi() the accumulator's high word. The sum is exact in an int64_t: every
 * product is below 2^46 and the total below 2^52, so the firmware's 56-bit
 * accumulator never wraps on the way.
 */
#ifndef GM2_BIQUAD_H
#define GM2_BIQUAD_H

#include <stdint.h>

#include "gm2_dsp_math.h"

#define GM2_BIQUAD_COEFS 5u

typedef struct {
  int64_t acc;  /* y[n-1], signed 48-bit */
  int32_t prev; /* y[n-2] */
} gm2_biquad;

static inline void gm2_biquad_init(gm2_biquad *stage) {
  stage->acc = 0;
  stage->prev = 0;
}

/* One stage, one sample. `x` holds the stage's input as {x[n-1], x[n-2], x[n]}
 * and leaves as its output in the same shape, {y[n-1], y[n-2], y[n]}, which is
 * what the next stage of a chain takes. Returns y[n] before its 24-bit store. */
static inline int64_t gm2_biquad_step(gm2_biquad *stage, const int32_t coef[GM2_BIQUAD_COEFS],
                                      int32_t x[3]) {
  const int64_t last = stage->acc;
  /* Only gm2_store48 and the initializer write the accumulator, so it is always a
   * 48-bit value and this 24-bit store cannot rail. */
  const int32_t y1 = (int32_t)gm2_floor_step24(last);
  const int32_t y2 = stage->prev;
  const int64_t acc = last +
                      8 * ((int64_t)coef[0] * x[0] + (int64_t)coef[1] * x[1] +
                           (int64_t)coef[2] * x[2] + (int64_t)coef[3] * y1) +
                      4 * (int64_t)coef[4] * y2;
  const int64_t limited = gm2_store48(acc);
  stage->prev = y1;
  stage->acc = limited;
  x[0] = y1;
  x[1] = y2;
  /* The 48-bit limit is the 24-bit one twenty-four places up, so the word the
   * output store reads out of the limited accumulator is already inside the rails. */
  x[2] = (int32_t)gm2_floor_step24(limited);
  return acc;
}

#endif /* GM2_BIQUAD_H */
