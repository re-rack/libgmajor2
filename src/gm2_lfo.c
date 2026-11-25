#include "gm2_lfo.h"

#include "gm2_dsp_math.h"
#include "gm2_tables.h"

/* Past 2^52 a double has no fraction left, and a NaN or an infinity has none
 * either: those positions sit on a cycle boundary. */
#define LFO_CYCLES_LIMIT 4503599627370496.0

/* 2^22, the low end of a normalized mantissa. */
#define LFO_MANTISSA_MIN 4194304.0

static const gm2_swf k_millisecond = GM2_SWF_CONST(0x418937u, 0x76u); /* 0.001 */
static const gm2_swf k_beat_min = GM2_SWF_CONST(0x666666u, 0x7Du);    /* 0.2 s */
static const gm2_swf k_log10 = GM2_SWF_CONST(0x6F2DECu, 0x7Eu);       /* 1 / ln 10 */
static const gm2_swf k_decade = GM2_SWF_CONST(0x500000u, 0x86u);      /* 80 steps */
static const gm2_swf k_speed_base = GM2_SWF_CONST(0x600000u, 0x84u);  /* 24 */
static const gm2_swf k_round = GM2_SWF_CONST(0x400008u, 0x7Fu);       /* just above 0.5 */

gm2_swf gm2_lfo_beat(double beat_ms) {
  double ms = beat_ms;
  if (!(ms == ms)) {
    ms = GM2_LFO_BEAT_MS_DEFAULT;
  } else if (ms < GM2_LFO_BEAT_MS_MIN) {
    ms = GM2_LFO_BEAT_MS_MIN;
  } else if (ms > GM2_LFO_BEAT_MS_MAX) {
    ms = GM2_LFO_BEAT_MS_MAX;
  }
  /* The nearest value on the 24-bit mantissa grid. For a whole millisecond that
   * is what P:0x000BFD makes of the integer the device's Tap Time holds. */
  gm2_swf beat = {0, 150};
  while (ms < LFO_MANTISSA_MIN) {
    ms *= 2.0;
    beat.e -= 1;
  }
  beat.m = (int32_t)(ms + 0.5);
  if (beat.m > GM2_W24_MAX) { /* the rounding carried into the next power of two */
    beat.m = (int32_t)LFO_MANTISSA_MIN;
    beat.e += 1;
  }
  return beat;
}

/* The rate of a Speed step: the fastest one sits at the routine's zero. */
static gm2_swf lfo_speed_rate(int speed) { return gm2_swf_hz80(speed - GM2_LFO_SPEED_MAX); }

/* P:0x08F30E: one cycle per note value, on a beat of at least 0.2 s, capped at
 * the fastest Speed. */
static gm2_swf lfo_tempo_rate(unsigned note, double beat_ms) {
  gm2_swf beat = gm2_swf_mul(gm2_lfo_beat(beat_ms), k_millisecond);
  if (gm2_swf_cmp(beat, k_beat_min) < 0) {
    beat = k_beat_min;
  }
  const gm2_swf cycle = gm2_swf_div(gm2_swf_mul(gm2_swf_from_int(gm2_note_beats[note]), beat),
                                    gm2_swf_from_int(gm2_note_divisor[note]));
  gm2_swf rate = gm2_swf_div(gm2_swf_one, cycle);
  const gm2_swf fastest = lfo_speed_rate(GM2_LFO_SPEED_MAX);
  if (gm2_swf_cmp(rate, fastest) > 0) {
    rate = fastest;
  }
  return rate;
}

gm2_swf gm2_lfo_rate(int speed, unsigned note, double beat_ms) {
  return note != 0u ? lfo_tempo_rate(note, beat_ms) : lfo_speed_rate(speed);
}

int32_t gm2_lfo_step(const gm2_rate_words *rate, gm2_swf hz) {
  return gm2_swf_to_q23(gm2_swf_mul(hz, rate->period));
}

int32_t gm2_lfo_rate_to_speed(gm2_swf hz) {
  gm2_swf steps = gm2_swf_sub(gm2_swf_mul(gm2_swf_ln(hz), k_log10), gm2_swf_one);
  steps = gm2_swf_sub(gm2_swf_mul(steps, k_decade), k_speed_base);
  return gm2_swf_to_int(gm2_swf_add(steps, k_round)) + GM2_LFO_SPEED_MAX;
}

int32_t gm2_lfo_phase_at(double beats, unsigned note) {
  const double cycles = beats * (double)gm2_note_divisor[note] / (double)gm2_note_beats[note];
  if (!(cycles > -LFO_CYCLES_LIMIT && cycles < LFO_CYCLES_LIMIT)) {
    return 0;
  }
  double fraction = cycles - (double)(int64_t)cycles;
  if (fraction < 0.0) {
    fraction += 1.0;
  }
  /* A full cycle wraps to 0: rounding up to 0x800000 is the next cycle's start, not
   * the last unit of this one. */
  const int32_t phase = (int32_t)(fraction * 8388608.0 + 0.5);
  return phase & 0x7FFFFF;
}
