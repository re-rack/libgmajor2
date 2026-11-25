/* The six sample-rate values the rate install P:0x082DC0 leaves in
 * X:0x099429..0x099434, on the firmware's software float. Setup code takes them
 * from here wherever the firmware reads those pairs.
 */
#ifndef GM2_RATE_H
#define GM2_RATE_H

#include "gm2_swf.h"

typedef struct {
  gm2_swf rate;        /* X:0x099429/0x09942A: the rate in Hz */
  gm2_swf rate_khz;    /* X:0x09942B/0x09942C: rate * 0.001 */
  gm2_swf omega;       /* X:0x09942D/0x09942E: 2 pi / rate */
  gm2_swf minus_omega; /* X:0x09942F/0x099430: -2 pi / rate */
  gm2_swf period;      /* X:0x099431/0x099432: 1 / rate */
  gm2_swf period_ms;   /* X:0x099433/0x099434: 1000 / rate */
} gm2_rate_words;

/* The six values for a rate of 1..GM2_SAMPLE_RATE_MAX Hz, computed as P:0x082DC0
 * computes them. */
void gm2_rate_words_init(gm2_rate_words *words, unsigned rate);

#endif /* GM2_RATE_H */
