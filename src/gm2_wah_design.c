#include "gm2_wah_design.h"

#include "gm2_dsp_math.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  RANGE_MIN = 0,
  RANGE_MAX = 1,
};

int32_t gm2_wah_design_shift(int range) {
  return gm2_clamp_raw(range, RANGE_MIN, RANGE_MAX) == RANGE_MAX ? 1 : 2;
}
