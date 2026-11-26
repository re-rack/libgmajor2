#include "gm2_handback.h"

#include "gm2_dsp_math.h"

enum { HANDBACK_FADE_SHIFT = 10 };

_Static_assert(GM2_HANDBACK_FADE_FRAMES == 1u << HANDBACK_FADE_SHIFT,
               "the fade divides by a shift");

void gm2_handback_init(gm2_handback *handback) {
  handback->own = 0;
  handback->fading = 0;
  handback->fade = 0;
  handback->calm = 0;
  handback->settled = 0;
}

void gm2_handback_start(gm2_handback *handback) {
  if (!handback->own) {
    gm2_handback_init(handback);
    handback->own = 1;
    return;
  }
  handback->fading = 0;
  handback->calm = 0;
  handback->settled = 0;
}

int gm2_handback_frame(gm2_handback *handback, int settled, const int32_t exact[GM2_CHANNELS],
                       const int32_t own[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  if (settled) {
    int calm = 1;
    for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
      const int32_t gap = exact[channel] - own[channel];
      calm &= gap <= GM2_HANDBACK_CALM_LSB && gap >= -GM2_HANDBACK_CALM_LSB;
    }
    handback->calm = calm ? handback->calm + 1u : 0u;
    handback->settled++;
    if (handback->calm >= GM2_HANDBACK_CALM_FRAMES ||
        handback->settled >= GM2_HANDBACK_WAIT_FRAMES) {
      handback->fading = 1;
    }
  } else {
    handback->calm = 0;
    handback->settled = 0;
    handback->fading = 0;
  }

  if (handback->fading) {
    handback->fade++;
  } else if (handback->fade != 0u) {
    handback->fade--;
  }
  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    const int64_t gap = (int64_t)exact[channel] - own[channel];
    out[channel] =
        own[channel] + (int32_t)gm2_asr_n(gap * handback->fade + (GM2_HANDBACK_FADE_FRAMES >> 1),
                                          HANDBACK_FADE_SHIFT);
  }
  if (handback->fade < GM2_HANDBACK_FADE_FRAMES) {
    return 1;
  }
  gm2_handback_init(handback);
  return 0;
}
