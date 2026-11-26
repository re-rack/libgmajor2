#include "gm2_pitch_core.h"

#include "gm2_dsp_math.h"
#include "gm2_job.h"
#include "gm2_process.h"
#include "gm2_tables.h"

#include <string.h>

/* The buffers' AGU windows, and the detector buffer's address, which the fine
 * search's own AGU steps work on. */
enum {
  PITCH_DET_MASK = GM2_PITCH_CORE_DET_WORDS - 1u,
  PITCH_RING_MASK = GM2_PITCH_CORE_RING_WORDS - 1u,
  PITCH_DET_BASE = 0x082000,
};

/* The voices: the immediates P:0x000D3C..0x000E4D carries, and the step word of
 * the two ratios' one-pole record. */
enum {
  PITCH_TAP_HALF = 0x008000,          /* P:0x000D55 */
  PITCH_FADE_STEP_MIN = 0x007C1F,     /* P:0x000E38 */
  PITCH_DELAY_STEP = 0x00350B,        /* P:0x000DB5, P:0x000DD7 */
  PITCH_WINDOW_GAIN = 0x0CCCCD,       /* P:0x000DC0 */
  PITCH_WINDOW_SLICE = 0x02F200,      /* P:0x000DC8 */
  PITCH_RING_SHIFT = 15,              /* P:0x000DCD */
  PITCH_CYCLE_CHUNKS = 0x20,          /* P:0x000DA0 */
  PITCH_HARM_POINT = 0x000024,        /* P:0x000E14, three curve points per note */
  PITCH_HARM_MID = 0x000010,          /* P:0x000E0D */
  PITCH_HARM_SIGN = -0x080000,        /* P:0x000E28 */
  PITCH_HARM_UNIT = 0x000100,         /* P:0x000E2B */
  PITCH_RATIO_SMOOTH_STEP = 0x020DC6, /* X:0x0987D0 */
};

/* The detector: the immediates of P:0x0003F2..0x00089C, and the words its boot
 * code leaves in X:0x0429..0x0587 for good. */
enum {
  PITCH_DET_POLE = -0x400000,         /* X:0x0429 */
  PITCH_COARSE_GAIN = 0x100000,       /* P:0x00059F, the template's scale */
  PITCH_COARSE_GROUP_WORDS = 16,
  PITCH_ENERGY_ROUND = 0x100,         /* P:0x0005BB, the normalization's low bit */
  PITCH_PARABOLA_MID = 0x400000,      /* P:0x000743 */
  PITCH_PEAKS = 16,                   /* P:0x0006BF */
  PITCH_PEAK_WORDS = 2 * PITCH_PEAKS, /* X:0x0588..0x05A7: a period and a confidence each */
  PITCH_PEAK_SHARE = 0x4CCCCD,        /* X:0x0577 */
  PITCH_FIRST_SHARE = 0x7D70A4,       /* X:0x0576 */
  PITCH_RATIO_LIMIT = 0x555571,       /* X:0x0581 */
  PITCH_CONF_HOLD = 0x666666,         /* X:0x0582 */
  PITCH_CONF_SWITCH = 0x733333,       /* X:0x0583 */
  PITCH_CONF_MIN = 0x4CCCCD,          /* X:0x0585 */
  PITCH_ENERGY_SWITCH = 0x000347,     /* X:0x0584 */
  PITCH_ENERGY_MIN = 8,               /* X:0x0586 */
  PITCH_COUNT_MAX = 5,                /* X:0x0587 */
  PITCH_FINE_GAIN = 0x7C0000,         /* P:0x000611 */
  PITCH_FINE_STEPS = 14,              /* P:0x00061C REP #$0E and P:0x000638 DOR #$000E */
  PITCH_FINE_MIDDLE = 3,              /* P:0x00065B, the interpolated lag */
  PITCH_FINE_UNIT = 0x000080,         /* P:0x0006AC, one lag in Q8 */
  PITCH_FINE_BIAS = -3,               /* P:0x000688 */
  PITCH_FINE_STRIDE = 0x000800,       /* P:0x00068B */
  PITCH_FINE_WINDOW = 0x000160,       /* X:0x056D */
  PITCH_FINE_LAG_STEP = 0x000002,     /* P:0x00058C */
  PITCH_TOLERANCE = 0x200000,         /* P:0x000680 */
};

/* The words the boot code leaves the routine to start on. */
enum {
  PITCH_REST_PERIOD = 0x0186A0,        /* P:0x000B6D: X:0x0410, the ring and each voice's span */
  PITCH_REST_PEAK = 0x002F2B,          /* P:0x000D7C: X:0x0436 and X:0x043D */
  PITCH_REST_FINE_WINDOW = 0x000800,   /* P:0x000BFC: X:0x056C */
  PITCH_REST_FINE_ESTIMATE = 0x00002C, /* P:0x000DAD: X:0x056E */
  PITCH_REST_FINE_STRIDE = 0x000010,   /* P:0x000DA8: X:0x056F */
};

/* What each search does on its next chunk; X:0x0432 and X:0x0569 hold the
 * routine's address. */
enum {
  PITCH_COARSE_SOLVE,   /* P:0x000604: wait for the chunk's job run to solve */
  PITCH_COARSE_CAPTURE, /* P:0x00059A */
  PITCH_COARSE_SCAN,    /* P:0x0005D2 */
};

enum {
  PITCH_FINE_CAPTURE, /* P:0x000606 */
  PITCH_FINE_SCAN,    /* P:0x00062B */
  PITCH_FINE_SOLVE,   /* P:0x000656 */
  PITCH_FINE_CLEAR,   /* P:0x0006B4 */
};

/* X:0x042B..0x042E: the words between two points of the coarse template, one
 * stride for each quarter of it. */
static const int32_t k_coarse_step[GM2_PITCH_CORE_COARSE_WORDS / PITCH_COARSE_GROUP_WORDS] = {
    5, 7, 17, 31};

/* The store point of the coarse scan's MACR: the template's word less the
 * sample's eighth (the gain is 2^20), rounded half to even onto the 24-bit grid.
 * The fraction is a whole eighth, which is what the tie below tests. The 24-bit
 * limit cannot fire: the capture scales a 24-bit sample by 2^20, so a template
 * word is a sample's eighth and stays within 2^20, the sample within 2^23, and
 * the difference within 2^24, an eighth of the rail. */
static int32_t pitch_scan_word(int32_t template_word, int32_t sample) {
  const int32_t n = template_word * 8 - sample;
  return (n + 3 + ((n >> 3) & 1)) >> 3;
}

/* `move a,x1` and `move a,x:(ea)`: the A1 field limited to 24 bits, which also
 * sets the L flag the voice record's splice logic tests. */
static int32_t pitch_store(int64_t acc, int *limit) {
  const int64_t word = gm2_floor_step24(acc);
  if (word > GM2_W24_MAX || word < GM2_W24_MIN) {
    *limit = 1;
  }
  return gm2_store24(acc);
}

/* The audio buffer runs with the AGU's 20,000-word modulo window M2, which is not
 * a power of two. The index is at most 2^23 words either way of the write
 * index; 512 moduli lift the sum past zero, so the remainder is unsigned and
 * already the window's offset. */
static uint32_t pitch_audio_step(uint32_t at, int32_t step) {
  const uint32_t lift = 512u * GM2_PITCH_CORE_AUDIO_WORDS + (uint32_t)((int32_t)at + step);
  return lift % GM2_PITCH_CORE_AUDIO_WORDS;
}

/* The detector buffer is the AGU's 2048-word modulo window M4 at X:0x082000..0x0827FF. */
static uint32_t pitch_det_step(uint32_t at, int32_t step) {
  return (at + (uint32_t)step) & PITCH_DET_MASK;
}

/* One address update of the AGU's modulo arithmetic, R + N or R - N under the
 * modulus m + 1: a step of exactly the modulus moves the address a whole window
 * on, out of it, and any other step wraps inside the window. */
static inline uint32_t pitch_agu_mod_step(uint32_t r, uint32_t n_u24, uint32_t m, int is_sub) {
  int32_t n = gm2_s24(n_u24);
  if (is_sub != 0) {
    n = -n;
  }
  const uint32_t mod = m + 1u;
  uint32_t k = 1u;
  while (k < mod) {
    k <<= 1;
  }
  const uint32_t mask = k - 1u;
  const int32_t abs_n = n < 0 ? -n : n;
  if ((uint32_t)abs_n == mod) {
    return (uint32_t)((int32_t)r + n) & 0xFFFFFFu;
  }
  const int32_t off = (int32_t)(r & mask) + n;
  int32_t sum = (int32_t)r + n;
  if (n > 0 && off > (int32_t)m) {
    sum -= (int32_t)mod;
  } else if (n < 0 && off < 0) {
    sum += (int32_t)mod;
  }
  return (uint32_t)sum & 0xFFFFFFu;
}

/* The detector buffer at an AGU address. The fine search's first capture steps a
 * whole window past the buffer, where the words read as zero. */
static int32_t pitch_read_det_buf(const gm2_pitch_core_state *state, uint32_t addr) {
  return (addr >= PITCH_DET_BASE && addr < PITCH_DET_BASE + GM2_PITCH_CORE_DET_WORDS)
             ? state->det_buf[addr - PITCH_DET_BASE]
             : 0;
}

/* One DIV instruction: a conditional add or subtract of the divisor's word, then
 * a left shift that takes in the previous step's carry and leaves this step's
 * quotient bit in bit 0. */
static uint64_t pitch_div_step(uint64_t acc, int32_t den, int *carry) {
  const int msb = (int)((acc >> 55) & 1u);
  const int64_t divisor = gm2_word_acc(den);
  acc = ((acc << 1) | (uint64_t)*carry) & UINT64_C(0xFFFFFFFFFFFFFF);
  int64_t step = ((int64_t)(acc << 8)) >> 8;
  if (msb == (den < 0 ? 1 : 0)) {
    step -= divisor;
  } else {
    step += divisor;
  }
  *carry = (int)(((uint64_t)step >> 55) & 1u) ^ 1;
  return ((uint64_t)step & 0xFFFFFFFF000000ULL) | (acc & 0xFFFFFFULL);
}

/* P:0x00087E: the routine brackets 24 DIV steps with REP #$18. The dividend
 * arrives in A1, the routine moves the divisor into A0 and returns the quotient
 * from A0. */
static int32_t pitch_div24(int32_t num, int32_t den) {
  uint64_t a =
      ((uint64_t)(num < 0 ? 0xFFu : 0u) << 48) | ((uint64_t)gm2_u24(num) << 24) | gm2_u24(den);
  int carry = 0;
  for (unsigned i = 0; i < 24u; i++) {
    a = pitch_div_step(a, den, &carry);
  }
  return gm2_s24((uint32_t)(a & 0xFFFFFFULL));
}

/* P:0x000884: log2 of a Q23 word through fifteen NORM steps, the correction word
 * and the six-word polynomial X:0x0440. */
static int32_t pitch_log2(int32_t value) {
  const int32_t *poly = gm2_pitch_log2_poly;
  int64_t a = gm2_word_acc(value);
  int32_t norm = 0;
  for (unsigned i = 0; i < 15u; i++) {
    const int high = (int)(((uint64_t)a >> 47) & 1u);
    const int next = (int)(((uint64_t)a >> 46) & 1u);
    if (gm2_es(a) == 0 && high == next) {
      a = gm2_asl(a);
      norm--;
    }
  }
  a = gm2_asl(a);
  a = gm2_acc56(a + gm2_word_acc(-0x800000));
  if (a < 0) {
    return GM2_W24_MIN;
  }
  int32_t x0 = gm2_store24(a);
  int64_t b = gm2_mpyr(x0, x0);
  int32_t x1 = gm2_store24(b);
  a = gm2_mpy(poly[0], x0);
  b = gm2_mpyr(x1, x0);
  a = gm2_mac(a, x1, poly[1]);
  x1 = gm2_store24(b);
  b = gm2_mpyr(x1, x0);
  a = gm2_mac(a, x1, poly[2]);
  x1 = gm2_store24(b);
  b = gm2_mpyr(x1, x0);
  norm--;
  a = gm2_mac(a, x1, poly[3]);
  x1 = gm2_store24(b);
  const int32_t norm_word = (int32_t)((uint32_t)norm & 0xFFFFu);
  b = gm2_mpyr(x1, x0);
  x0 = norm_word;
  a = gm2_mac(a, x1, poly[4]);
  x1 = gm2_store24(b);
  b = gm2_mpy(0x040000, x0);
  const int32_t scale = gm2_lo24(b);
  a = gm2_macr(a, x1, poly[5]);
  a = gm2_acc56(a + gm2_word_acc(scale));
  return gm2_store24(a);
}

/* P:0x000739: the parabola through the three AMDF words at and around `at`
 * returns the vertex offset and its height. */
static void pitch_parabola(const int32_t *amdf, unsigned at, int32_t *delta, int32_t *height) {
  const int32_t low = amdf[at - 1u];
  const int32_t mid = amdf[at];
  const int32_t high = amdf[at + 1u];
  int64_t b = gm2_acc56(gm2_word_acc(mid) - gm2_word_acc(low));
  const int64_t a = b;
  b = gm2_acc56(b + gm2_word_acc(mid) - gm2_word_acc(high));
  const int32_t divisor = gm2_store24(b);
  const int32_t quotient = pitch_div24(gm2_store24(a), divisor);
  const int64_t offset = gm2_acc56(gm2_word_acc(quotient) - gm2_word_acc(PITCH_PARABOLA_MID));
  const int32_t vertex = gm2_store24(offset);
  const int32_t square = gm2_store24(gm2_mpyr(vertex, vertex));
  b = gm2_acc56(gm2_asr_n(gm2_mpy(divisor, square), 1) + gm2_word_acc(mid));
  *delta = vertex;
  *height = gm2_store24(b);
}

/* P:0x0006BB: the peaks of the difference function above a share of its largest
 * word become period, confidence pairs, at most sixteen. The count of `lags`
 * words falls on every word the scan reads, the negative word that ends a run
 * included, and the four loops test it where the firmware does. Returns the
 * number of pairs. */
static int32_t pitch_pick_peaks(const int32_t *amdf, int32_t lags, int32_t *peaks) {
  uint32_t at = 0;
  int32_t left = lags;
  unsigned words = 0;
  if (left == 0) {
    return 0;
  }
  for (;;) {
    const int32_t word = amdf[at++];
    left--;
    if (word < 0) {
      break;
    }
    if (left == 0) {
      return 0;
    }
  }
  if (left == 0) {
    return 0;
  }
  /* P:0x0006D4: 2E8000 `move #$80,a` loads -0x800000 into A. */
  int32_t largest = GM2_W24_MIN;
  for (;;) {
    const int32_t word = amdf[at++];
    left--;
    if (largest < word) {
      largest = word;
    }
    if (left == 0) {
      break;
    }
  }
  const int32_t threshold = gm2_store24(gm2_mpy(largest, PITCH_PEAK_SHARE));

  at = 0;
  left = lags;
  for (;;) {
    const int32_t word = amdf[at++];
    left--;
    if (word < 0) {
      break;
    }
    if (left == 0) {
      return 0;
    }
  }

  while (left != 0) {
    int32_t word = 0;
    int above = 0;
    for (;;) {
      word = amdf[at++];
      left--;
      if (word > threshold) {
        above = 1;
        break;
      }
      if (left == 0) {
        break;
      }
    }
    if (above == 0 || left == 0) {
      break;
    }
    int32_t peak = word;
    uint32_t peak_at = at;
    int32_t last = word;
    int ended = 0;
    for (;;) {
      word = amdf[at++];
      left--;
      if (word < 0) {
        ended = 1;
        break;
      }
      last = word;
      if (peak < word) {
        peak = word;
        peak_at = at;
      }
      if (left == 0) {
        break;
      }
    }
    if (ended == 0 && peak < last) {
      break;
    }
    const unsigned middle = peak_at - 1u;
    int32_t delta = 0;
    int32_t height = 0;
    pitch_parabola(amdf, middle, &delta, &height);
    const int32_t offset = gm2_lo24(gm2_mpy((int32_t)middle, 0x000200));
    const int64_t period = gm2_round24(gm2_acc56(gm2_mpy(delta, 0x000400) + gm2_word_acc(offset)));
    peaks[words++] = gm2_store24(period);
    peaks[words++] = pitch_div24(height, amdf[0]);
    if (words / 2 >= (unsigned)PITCH_PEAKS) {
      break;
    }
  }
  return (int32_t)(words / 2);
}

/* P:0x00074D: the strongest peak's period, the first peak inside the period
 * window that stands a share of the strongest one tall, and the peak nearest the
 * locked period. */
static void pitch_select_peaks(gm2_pitch_core_state *state, const gm2_pitch_core_config *config,
                               const int32_t *peaks, int32_t count) {
  if (count == 0) {
    state->first_conf = 0;
    return;
  }
  int32_t best_conf = 0;
  int32_t best_period = config->max_period;
  for (int32_t i = 0; i < count; i++) {
    const int32_t period = peaks[2u * (unsigned)i];
    const int32_t conf = peaks[2u * (unsigned)i + 1u];
    if (best_conf < conf) {
      best_conf = conf;
      best_period = period;
    }
  }
  state->peak_period = best_period;

  const int32_t threshold = gm2_store24(gm2_mpy(best_conf, PITCH_FIRST_SHARE));
  int found = 0;
  for (int32_t i = 0; i < count; i++) {
    const int32_t period = peaks[2u * (unsigned)i];
    const int32_t conf = peaks[2u * (unsigned)i + 1u];
    if (period >= config->min_period && period <= config->max_period && conf >= threshold) {
      state->first_period = period;
      state->first_conf = conf;
      found = 1;
      break;
    }
  }
  if (found == 0) {
    state->first_conf = 0;
    return;
  }

  int32_t nearest = GM2_W24_MAX;
  int32_t nearest_index = 0;
  for (int32_t i = 0; i < count; i++) {
    const int32_t period = peaks[2u * (unsigned)i];
    const int32_t diff =
        gm2_store24(gm2_acc56(gm2_word_acc(period) - gm2_word_acc(state->locked_period)));
    if (gm2_abs56(gm2_word_acc(diff)) < gm2_abs56(gm2_word_acc(nearest))) {
      nearest = diff;
      nearest_index = i;
    }
  }
  state->near_period = peaks[2u * (unsigned)nearest_index];
}

/* P:0x0007BA, P:0x0007DA and P:0x000865: the two periods stay within `limit` of
 * each other in both directions. */
static int pitch_ratio_within(int32_t first, int32_t second, int32_t limit) {
  if (gm2_store24(gm2_mpy(limit, first)) > second) {
    return 0;
  }
  if (gm2_store24(gm2_mpy(second, limit)) > first) {
    return 0;
  }
  return 1;
}

/* P:0x00079F: the hysteresis between the first good peak, the locked period and
 * a rival, which gives the tracked period. */
static void pitch_track_period(gm2_pitch_core_state *state) {
  if (state->coarse_energy[0] < PITCH_ENERGY_MIN || state->first_conf < PITCH_CONF_MIN) {
    state->alt_count = 0;
    state->locked_count = 0;
    if (pitch_ratio_within(state->first_period, state->locked_period, PITCH_RATIO_LIMIT) != 0) {
      state->locked_period = state->first_period;
      state->tracked_period = state->first_period;
    }
    return;
  }

  if (state->locked_count == 0 ||
      pitch_ratio_within(state->first_period, state->locked_period, PITCH_RATIO_LIMIT) != 0) {
    state->alt_count = state->alt_count > 0 ? state->alt_count - 1 : 0;
    if (state->first_conf >= PITCH_CONF_HOLD) {
      const int32_t count = state->locked_count + 1;
      state->locked_count = count > PITCH_COUNT_MAX ? PITCH_COUNT_MAX : count;
    } else {
      state->locked_count = state->locked_count > 0 ? state->locked_count - 1 : 0;
    }
    state->locked_period = state->first_period;
    state->tracked_period = state->first_period;
    return;
  }

  state->locked_count = state->locked_count > 0 ? state->locked_count - 1 : 0;
  if (state->first_conf >= PITCH_CONF_SWITCH && state->coarse_energy[0] >= PITCH_ENERGY_SWITCH) {
    if (pitch_ratio_within(state->first_period, state->alt_period, PITCH_RATIO_LIMIT) == 0) {
      state->alt_count = 0;
    }
    state->alt_period = state->first_period;
    const int32_t count = state->alt_count + 1;
    state->alt_count = count > PITCH_COUNT_MAX ? PITCH_COUNT_MAX : count;
    if (state->locked_count < state->alt_count) {
      const int32_t locked = state->locked_count;
      const int32_t alternate = state->alt_count;
      state->alt_count = locked;
      state->locked_count = alternate;
      state->alt_period = state->locked_period;
      state->locked_period = state->first_period;
      state->tracked_period = state->first_period;
      return;
    }
  } else {
    state->alt_count = state->alt_count > 0 ? state->alt_count - 1 : 0;
  }

  const int32_t chosen =
      pitch_ratio_within(state->near_period, state->locked_period, PITCH_RATIO_LIMIT) != 0
          ? state->near_period
          : state->locked_period;
  state->locked_period = chosen;
  state->tracked_period = chosen;
}

/* P:0x0003F2..P:0x00042F: the chunk's four frames, summed to mono, through the
 * biquad Y:0x0279..Y:0x027D and the one-pole X:0x0429 into the detector
 * buffer. Every sum is a 24-bit word and at most six products of two 24-bit
 * words, so it stays under 2^50 and the accumulator cannot wrap. */
static void pitch_detector_filter(gm2_pitch_core_state *state) {
  int32_t mono[GM2_CHUNK_FRAMES];
  for (unsigned i = 0; i < GM2_CHUNK_FRAMES; i++) {
    mono[i] = gm2_store24(((int64_t)state->chunk[2u * i] + state->chunk[2u * i + 1u]) * 0x800000);
  }

  const int32_t b0 = gm2_pitch_det_biquad[0];
  const int32_t b1 = gm2_pitch_det_biquad[1];
  const int32_t b2 = gm2_pitch_det_biquad[2];
  const int32_t a1 = gm2_pitch_det_biquad[3];
  const int32_t a2 = gm2_pitch_det_biquad[4];
  for (unsigned i = 0; i < GM2_CHUNK_FRAMES; i++) {
    const int32_t in = mono[i];
    const int32_t in1 = state->det_biquad[0];
    const int32_t in2 = state->det_biquad[1];
    const int32_t out1 = state->det_biquad[2];
    const int32_t out2 = state->det_biquad[3];
    int64_t a = 2 * (int64_t)out1 * a1;
    a += 2 * (int64_t)out1 * a1;
    a += 2 * (int64_t)out2 * a2;
    a += 2 * (int64_t)in * b0;
    a += 2 * (int64_t)in1 * b1;
    a += 2 * (int64_t)in2 * b2;
    const int32_t out = gm2_store24(a);
    state->det_biquad[3] = out1;
    state->det_biquad[1] = in1;
    state->det_biquad[0] = in;
    state->det_biquad[2] = out;
    mono[i] = out;
  }

  const int32_t coef = PITCH_DET_POLE;
  int64_t a = gm2_word_acc(state->det_low_pass);
  int32_t previous = state->det_low_pass;
  uint32_t at = state->det_at;
  for (unsigned i = 0; i < GM2_CHUNK_FRAMES; i++) {
    a -= 2 * (int64_t)coef * mono[i];
    a = gm2_round24(a + 2 * (int64_t)coef * previous);
    state->det_buf[at] = gm2_store24(a);
    at = (at - 1u) & PITCH_DET_MASK;
    previous = gm2_store24(a);
  }
  state->det_low_pass = previous;
  state->det_at = at;
}

/* P:0x00059A: the template, its energy and the shift that normalizes it. Two
 * reads lead the four groups, both stepping with the first stride, so the strides
 * change one read past every sixteen. */
static void pitch_coarse_capture(gm2_pitch_core_state *state, const gm2_pitch_core_config *config) {
  uint32_t at = (state->det_at + 1u) & PITCH_DET_MASK;
  for (unsigned word = 0; word < GM2_PITCH_CORE_COARSE_WORDS; word++) {
    const int32_t sample = state->det_buf[at];
    state->coarse_template[word] = gm2_store24(gm2_mpyr(sample, PITCH_COARSE_GAIN));
    const unsigned group = word >= 2u ? (word - 2u) / PITCH_COARSE_GROUP_WORDS : 0u;
    at = pitch_det_step(at, k_coarse_step[group]);
  }
  int64_t energy = 0;
  for (unsigned word = 0; word < GM2_PITCH_CORE_COARSE_WORDS; word++) {
    energy = gm2_mac(energy, state->coarse_template[word], state->coarse_template[word]);
  }
  state->coarse_energy[0] = gm2_store24(energy);
  state->coarse_energy[1] = gm2_lo24(energy);
  energy |= PITCH_ENERGY_ROUND;
  energy = gm2_asr_n(energy, 1);
  int32_t norm = 0;
  for (unsigned i = 0; i < 8u; i++) {
    const int high = (int)(((uint64_t)energy >> 47) & 1u);
    const int next = (int)(((uint64_t)energy >> 46) & 1u);
    if (gm2_es(energy) == 0 && high == next) {
      energy = gm2_asl(energy);
      norm--;
    }
  }
  energy = gm2_round24(energy);
  state->amdf[0] = gm2_store24(energy);
  state->coarse_shift = -norm;
  state->coarse_at = 1;
  state->coarse_end = 1u + (uint32_t)config->lag_count;
  state->coarse_state = PITCH_COARSE_SCAN;
}

/* P:0x0005D2: one lag of the difference function a chunk: the energy of the
 * template less the window at that lag, taken off the template's own and
 * normalized. */
static void pitch_coarse_scan(gm2_pitch_core_state *state) {
  uint32_t at = (state->det_at + 1u) & PITCH_DET_MASK;
  /* The energy sums 64 doubled squares of words within 2^21, below 2^49, so it
   * needs no 56-bit wrap. */
  int64_t energy = 0;
  unsigned word = 0;
  for (unsigned group = 0; group < GM2_PITCH_CORE_COARSE_WORDS / PITCH_COARSE_GROUP_WORDS;
       group++) {
    const int32_t step = k_coarse_step[group];
    unsigned end = word + PITCH_COARSE_GROUP_WORDS + (group == 0u ? 2u : 0u);
    if (end > GM2_PITCH_CORE_COARSE_WORDS) {
      end = GM2_PITCH_CORE_COARSE_WORDS;
    }
    for (; word < end; word += 2u) {
      const int32_t d0 = pitch_scan_word(state->coarse_template[word], state->det_buf[at]);
      at = pitch_det_step(at, step);
      const int32_t d1 = pitch_scan_word(state->coarse_template[word + 1u], state->det_buf[at]);
      at = pitch_det_step(at, step);
      energy += 2 * ((int64_t)d0 * d0 + (int64_t)d1 * d1);
    }
  }
  const int64_t captured =
      gm2_word_acc(state->coarse_energy[0]) | (int64_t)gm2_u24(state->coarse_energy[1]);
  energy = gm2_asr_n(energy, 1);
  int64_t acc = gm2_acc56(captured - energy);
  /* The capture leaves at most eight steps of two, and doubling that many times
   * wraps the same as one shift by their count. */
  if (state->coarse_shift > 0) {
    acc = gm2_asl_n(acc, (unsigned)state->coarse_shift);
  } else {
    acc = gm2_asr_n(acc, (unsigned)-state->coarse_shift);
  }
  acc = gm2_asr_n(acc, 1);
  acc = gm2_round24(acc);
  state->amdf[state->coarse_at] = gm2_store24(acc);
  state->coarse_at++;
  if (state->coarse_at >= state->coarse_end) {
    state->coarse_state = PITCH_COARSE_SOLVE;
  }
}

/* P:0x000606: the sixteen words of the fine template, a stride apart, and the
 * address the scan starts from. */
static void pitch_fine_capture(gm2_pitch_core_state *state) {
  const uint32_t n5 = (uint32_t)state->fine_estimate & 0xFFFFFFu;
  const uint32_t n4 = n5 >> 1;
  uint32_t p = (uint32_t)PITCH_DET_BASE + (state->det_at & PITCH_DET_MASK);
  p = pitch_agu_mod_step(p, n4, PITCH_DET_MASK, 1);
  p = pitch_agu_mod_step(p, (uint32_t)state->fine_window & 0xFFFFFFu, PITCH_DET_MASK, 0);
  uint32_t r5 = pitch_agu_mod_step(p, 1u, PITCH_DET_MASK, 0);
  r5 = pitch_agu_mod_step(r5, n5, PITCH_DET_MASK, 1);
  state->fine_at = r5;

  const uint32_t step = (uint32_t)state->fine_stride & 0xFFFFFFu;
  int32_t sample = pitch_read_det_buf(state, p);
  p = pitch_agu_mod_step(p, step, PITCH_DET_MASK, 0);
  int64_t scaled = gm2_mpyr(sample, PITCH_FINE_GAIN);
  sample = pitch_read_det_buf(state, p);
  p = pitch_agu_mod_step(p, step, PITCH_DET_MASK, 0);
  for (unsigned i = 0; i < PITCH_FINE_STEPS; i++) {
    state->fine_template[i] = gm2_store24(scaled);
    scaled = gm2_mpyr(sample, PITCH_FINE_GAIN);
    sample = pitch_read_det_buf(state, p);
    p = pitch_agu_mod_step(p, step, PITCH_DET_MASK, 0);
  }
  state->fine_template[PITCH_FINE_STEPS] = gm2_store24(scaled);
  scaled = gm2_mpyr(sample, PITCH_FINE_GAIN);
  state->fine_template[PITCH_FINE_STEPS + 1u] = gm2_store24(scaled);
  state->fine_best_score = GM2_W24_MIN;
  state->fine_lag = (int32_t)GM2_PITCH_CORE_FINE_LAGS - 1;
  state->fine_state = PITCH_FINE_SCAN;
}

/* P:0x00062B: one lag of the fine search a chunk: its score, and the best one so
 * far. A template word plus one product of two 24-bit words stays under 2^49 and
 * the score's sixteen doubled squares under 2^51, so the accumulator cannot wrap. */
static void pitch_fine_scan(gm2_pitch_core_state *state) {
  uint32_t p = pitch_agu_mod_step(state->fine_at, 1u, PITCH_DET_MASK, 1);
  state->fine_at = p;
  const uint32_t step = (uint32_t)state->fine_stride & 0xFFFFFFu;
  int32_t sample = pitch_read_det_buf(state, p);
  p = pitch_agu_mod_step(p, step, PITCH_DET_MASK, 0);
  int64_t acc = gm2_word_acc(state->fine_template[0]);
  acc = gm2_round24(acc - 2 * (int64_t)sample * PITCH_FINE_GAIN);
  sample = pitch_read_det_buf(state, p);
  p = pitch_agu_mod_step(p, step, PITCH_DET_MASK, 0);
  int64_t score = 0;
  int32_t previous = gm2_store24(acc);
  acc = gm2_word_acc(state->fine_template[1]);
  for (unsigned i = 0; i < PITCH_FINE_STEPS; i++) {
    acc = gm2_round24(acc - 2 * (int64_t)sample * PITCH_FINE_GAIN);
    sample = pitch_read_det_buf(state, p);
    p = pitch_agu_mod_step(p, step, PITCH_DET_MASK, 0);
    score -= 2 * (int64_t)previous * previous;
    previous = gm2_store24(acc);
    acc = gm2_word_acc(state->fine_template[i + 2u]);
  }
  acc = gm2_round24(acc - 2 * (int64_t)sample * PITCH_FINE_GAIN);
  score -= 2 * (int64_t)previous * previous;
  previous = gm2_store24(acc);
  score = gm2_round24(score - 2 * (int64_t)previous * previous);
  const int32_t lag = state->fine_lag;
  const int32_t value = gm2_store24(score);
  state->fine_score[lag] = value;
  if (state->fine_best_score < value) {
    state->fine_best_score = value;
    state->fine_best_lag = lag;
  }
  state->fine_lag = lag - 1;
  if (lag <= 0) {
    state->fine_state = PITCH_FINE_SOLVE;
  }
}

/* P:0x000656: the fine period at the parabola's vertex around the best lag, its
 * log2 for the harmonizer, and the words the next capture runs on, which stay
 * with the tracked period when the fine one strays from it. */
static void pitch_fine_solve(gm2_pitch_core_state *state) {
  const int32_t lag = state->fine_best_lag;
  const int32_t whole = state->fine_estimate + ((int32_t)GM2_PITCH_CORE_FINE_LAGS - 1) - lag;
  int32_t period = 0;
  if (gm2_abs56(lag - PITCH_FINE_MIDDLE) >= PITCH_FINE_MIDDLE) {
    period = gm2_lo24(gm2_mpy(whole, PITCH_FINE_UNIT));
  } else {
    const int32_t high = state->fine_score[lag + 1];
    const int32_t mid = state->fine_score[lag];
    const int32_t low = state->fine_score[lag - 1];
    /* P:0x000664's `b,x1` and P:0x000667's `move b,y0` limit the accumulator
     * word, while the subtraction that forms it wraps. */
    const int32_t numerator = gm2_store24(gm2_word_acc(mid) - gm2_word_acc(high));
    const int32_t denominator =
        gm2_store24(gm2_acc56(gm2_word_acc(2 * mid - high) - gm2_word_acc(low)));
    /* Nine DIV steps from P:0x00066E to P:0x000672, the whole lag written into
     * B0 after the first: the remaining eight scale it to 1/256 lag and leave
     * the fraction in the bits below it. */
    uint64_t b =
        ((uint64_t)(numerator < 0 ? 0xFFu : 0u) << 48) | ((uint64_t)gm2_u24(numerator) << 24);
    int carry = 0;
    for (unsigned i = 0; i < 9u; i++) {
      b = pitch_div_step(b, denominator, &carry);
      if (i == 0u) {
        b = (b & ~UINT64_C(0xFFFFFF)) | (uint64_t)gm2_u24(whole);
      }
    }
    const int32_t fraction = gm2_s24((uint32_t)(b & 0xFFFFFFu));
    period = gm2_store24(gm2_acc56(gm2_word_acc(fraction) - gm2_word_acc(PITCH_FINE_UNIT)));
  }
  state->log_period = gm2_store24(-gm2_word_acc(pitch_log2(period)));
  if (state->coarse_fresh != 0) {
    const int32_t tracked = state->tracked_period;
    state->coarse_fresh = 0;
    const int32_t tolerance = gm2_store24(gm2_mpyr(tracked, PITCH_TOLERANCE));
    const int32_t diff = period - tracked;
    if (gm2_abs56(diff) > gm2_abs56(tolerance)) {
      period = tracked;
    }
  }
  state->fine_estimate =
      gm2_store24(gm2_macr(gm2_word_acc(PITCH_FINE_BIAS), PITCH_TAP_HALF, period));
  state->fine_stride = gm2_store24(gm2_mpyr((int32_t)PITCH_FINE_STRIDE, period));
  state->fine_state = PITCH_FINE_CLEAR;
}

/* P:0x0003F2: the detector on the four frames the chunk buffer collected, one
 * step of each search. */
static void pitch_detector(gm2_pitch_core_state *state, const gm2_pitch_core_config *config) {
  pitch_detector_filter(state);

  if (state->coarse_state == PITCH_COARSE_CAPTURE) {
    pitch_coarse_capture(state, config);
  } else if (state->coarse_state == PITCH_COARSE_SCAN) {
    pitch_coarse_scan(state);
  }

  switch (state->fine_state) {
  case PITCH_FINE_CAPTURE:
    pitch_fine_capture(state);
    break;
  case PITCH_FINE_SCAN:
    pitch_fine_scan(state);
    break;
  case PITCH_FINE_SOLVE:
    pitch_fine_solve(state);
    break;
  default:
    state->fine_state = PITCH_FINE_CAPTURE;
    break;
  }
}

/* P:0x00056E: the coarse search's answer once its scan is over, and the window
 * the next fine capture starts from. */
static void pitch_coarse_solve(gm2_pitch_core_state *state, const gm2_pitch_core_config *config) {
  int32_t peaks[PITCH_PEAK_WORDS];
  const int32_t count = pitch_pick_peaks(state->amdf, config->lag_count, peaks);
  pitch_select_peaks(state, config, peaks, count);
  pitch_track_period(state);
  state->coarse_fresh = 1;
  int64_t a = PITCH_FINE_WINDOW;
  a = gm2_mac(a, config->lag_count, PITCH_FINE_LAG_STEP);
  state->fine_window = gm2_lo24(a);
  state->coarse_state = PITCH_COARSE_CAPTURE;
}

/* The audio buffer at the position the accumulator `index` holds, in words past
 * the write index: the word there and the next one, interpolated by the index's
 * low half. */
static inline int64_t pitch_tap(const gm2_pitch_core_state *state, int64_t index) {
  const int32_t fraction = (int32_t)(((uint32_t)index >> 1) & 0x7FFFFFu);
  const uint32_t at = pitch_audio_step(state->audio_at, gm2_store24(index));
  const int32_t word = state->audio_buf[at];
  /* The step of one word stays inside the window, so the next word is the one
   * after it, or the window's first. */
  const int32_t next = state->audio_buf[at + 1u == GM2_PITCH_CORE_AUDIO_WORDS ? 0u : at + 1u];
  /* The gap is under 2^25 and the fraction under 2^23, so the product stays
   * under 2^49. */
  return gm2_round24((int64_t)word * 0x1000000 + 2 * (int64_t)fraction * (next - word));
}

/* P:0x000D3C..P:0x000D6E, one voice: the crossfade and the first tap move on, a
 * crossfade that has run out with its tap outside the delay window starts a
 * splice, and the two taps blend into the voice's output.
 *
 * The de-popped path splices only once this voice's crossfade has run out, while
 * the firmware's also takes the frame's limit flag, which another store raised:
 * a voice that splices with its crossfade still running cuts it short and clicks. */
static void pitch_voice(const gm2_pitch_core_state *state, gm2_pitch_core_voice *voice, int *limit,
                        int depopped) {
  /* Every sum in the voice is two 24-bit words or their products, so it stays
   * under 2^49 and the accumulator cannot wrap. The crossfade's step taking it
   * past -1 raises the limit flag, which is what the firmware's splice test reads,
   * even when an earlier store of the frame raised it. */
  int32_t fade = pitch_store(((int64_t)voice->fade - voice->fade_step) * 0x1000000, limit);
  const int above = voice->offset_max < voice->offset;
  const int ready = depopped != 0 ? fade == GM2_W24_MIN : *limit != 0;
  if (ready && (above || voice->offset_min > voice->offset)) {
    const int64_t span = (int64_t)voice->span * 0x1000000;
    const int64_t next = span + (int64_t)voice->offset * 0x1000000;
    voice->jump = pitch_store(above ? -span : span, limit);
    fade = 0;
    voice->offset = pitch_store(above ? next - 2 * span : next, limit);
  }
  voice->fade = fade;

  /* The first tap reads at the offset, which then drifts by the voice's rate. */
  int64_t index = (int64_t)voice->offset * 0x10000;
  voice->offset = pitch_store(((int64_t)voice->offset - voice->rate) * 0x1000000, limit);
  const int32_t first = pitch_store(pitch_tap(state, index), limit);

  /* The second tap, a splice on, and the crossfade from it to the first. */
  index -= (int64_t)voice->jump * 0x10000;
  int64_t blend = pitch_tap(state, index);
  const int32_t second = pitch_store(blend, limit);
  blend += 2 * (int64_t)(-first) * fade;
  blend = gm2_round24(blend + 2 * (int64_t)fade * second);
  voice->out = pitch_store(blend, limit);
}

/* P:0x000E09..P:0x000E32: the Intelligent Pitch Shifter's two ratios, from the
 * detected pitch on each voice's curve. */
static void pitch_harmonize(gm2_pitch_core_state *state, const gm2_pitch_core_config *config) {
  for (unsigned voice = 0; voice < GM2_PITCH_CORE_VOICES; voice++) {
    const int32_t *curve = config->curve[voice];

    /* The note within the octave: the fraction of the log2 pitch past the
     * reference. */
    int64_t a = gm2_mpy(PITCH_HARM_MID, state->log_period);
    a = gm2_mac(a, -config->ref_offset, PITCH_HARM_MID);
    const int32_t offset = (int32_t)(((uint32_t)gm2_lo24(a) >> 1) & 0x7FFFFFu);
    int64_t index = gm2_mpy(PITCH_HARM_POINT, offset);
    const int32_t point = gm2_store24(index);
    const int32_t fraction = (int32_t)(((uint32_t)gm2_lo24(index) >> 1) & 0x7FFFFFu);
    const int32_t low = curve[(unsigned)point];
    const int32_t high = curve[(unsigned)point + 1u];
    int64_t value = gm2_mac(gm2_word_acc(low), -fraction, low);
    value = gm2_macr(value, high, fraction);
    value = gm2_round24(value);
    const int32_t note = gm2_store24(value);

    /* exp2 of the curve's interval, as a polynomial. */
    int64_t power = gm2_word_acc(gm2_pitch_exp2_poly[0]);
    int64_t square = gm2_mpyr(note, note);
    power = gm2_mac(power, gm2_pitch_exp2_poly[1], note);
    int32_t square_word = gm2_store24(square);
    square = gm2_mpyr(square_word, note);
    power = gm2_mac(power, square_word, gm2_pitch_exp2_poly[2]);
    square_word = gm2_store24(square);
    power = gm2_macr(power, square_word, gm2_pitch_exp2_poly[3]);
    int32_t step = gm2_store24(power);

    int64_t scaled = gm2_macr(gm2_word_acc(PITCH_HARM_SIGN), config->curve_gain[voice], step);
    step = gm2_store24(scaled);
    scaled = gm2_asl_n(gm2_round24(gm2_asl_n(gm2_mpy(PITCH_HARM_UNIT, step), 4)), 12);
    state->harmony[voice] = gm2_store24(scaled);
  }
}

/* The end of P:0x000DB1 and P:0x000DD3: the delay window a voice's first tap
 * stays in, and how far back in the period ring the voice reads, both from its
 * delay. */
static void pitch_window(gm2_pitch_core_voice *voice, const gm2_pitch_core_config *config) {
  const int64_t low = gm2_mac(gm2_word_acc(voice->delay), config->max_period, PITCH_WINDOW_GAIN);
  const int32_t slice = voice->delay - PITCH_WINDOW_SLICE;
  voice->offset_min = gm2_store24(low);
  voice->offset_max = gm2_store24(low + gm2_word_acc(config->max_period));
  voice->ring_tap = (slice <= PITCH_TAP_HALF ? PITCH_TAP_HALF : slice) >> PITCH_RING_SHIFT;
}

/* P:0x000DB1 and P:0x000DD3: one voice's delay a step towards `target`. */
static void pitch_delay_step(gm2_pitch_core_voice *voice, int32_t target) {
  int32_t delay = voice->delay;
  if (delay < target) {
    delay += PITCH_DELAY_STEP;
  } else if (delay > target) {
    delay -= PITCH_DELAY_STEP;
  }
  if (gm2_abs56(delay - target) < PITCH_DELAY_STEP) {
    delay = target;
  }
  voice->delay = delay;
}

/* P:0x000E33..P:0x000E4D: each voice's drift and its crossfade's step, from its
 * live ratio. */
static void pitch_rates(gm2_pitch_core_state *state, const gm2_pitch_core_config *config) {
  for (unsigned i = 0; i < GM2_PITCH_CORE_VOICES; i++) {
    gm2_pitch_core_voice *voice = &state->voice[i];
    voice->rate = state->ratio[i] >> 12;
    const int64_t step =
        gm2_store24(gm2_abs56(gm2_asl_n(gm2_mpy(voice->rate, config->rate_factor), 23)));
    voice->fade_step = step < PITCH_FADE_STEP_MIN ? PITCH_FADE_STEP_MIN : (int32_t)step;
  }
}

/* The rest of P:0x000D15..0x000E4D, which runs on the first frame of each chunk:
 * the detector, then the cycle's work for this chunk. */
static void pitch_chunk(gm2_pitch_core_state *state, const gm2_pitch_core_config *config) {
  state->chunk_at = 0;
  pitch_detector(state, config);

  int32_t count = state->cycle - 1;
  if (count <= 0) {
    /* The ring takes the period, and each voice its splice length out of it. */
    state->period_ring[state->ring_at] = state->period;
    state->ring_at = (state->ring_at - 1u) & PITCH_RING_MASK;
    for (unsigned i = 0; i < GM2_PITCH_CORE_VOICES; i++) {
      gm2_pitch_core_voice *voice = &state->voice[i];
      voice->span =
          state->period_ring[(state->ring_at + (uint32_t)voice->ring_tap) & PITCH_RING_MASK];
    }
    count = PITCH_CYCLE_CHUNKS;
  }
  state->cycle = count;

  if (count <= (int32_t)GM2_PITCH_CORE_VOICES) {
    /* The cycle's last chunks: one voice's delay each, the first voice last. */
    gm2_pitch_core_voice *voice = &state->voice[count - 1];
    pitch_delay_step(voice, config->delay[count - 1]);
    pitch_window(voice, config);
  } else if (count == (int32_t)GM2_PITCH_CORE_VOICES + 1) {
    if (config->harmonize != 0) {
      pitch_harmonize(state, config);
    }
    pitch_rates(state, config);
  }
}

/* The chunk's job run. P:0x0001E4 solves the coarse search once its scan is over
 * and takes the strongest peak's period when it lies in the window; the ratios,
 * the feedbacks, the pan gains and the Mix words ride one-pole jobs, the ratios
 * towards the harmonizer's where it runs. */
static void pitch_jobs(gm2_pitch_core_state *state, const gm2_pitch_core_config *config) {
  if (state->coarse_state == PITCH_COARSE_SOLVE) {
    pitch_coarse_solve(state, config);
  }
  if (state->peak_period <= config->max_period && state->peak_period >= config->min_period) {
    state->period = state->peak_period;
  }

  const int32_t *ratio = config->harmonize != 0 ? state->harmony : config->ratio;
  for (unsigned i = 0; i < GM2_PITCH_CORE_VOICES; i++) {
    state->ratio[i] = gm2_job_smooth(state->ratio[i], ratio[i], PITCH_RATIO_SMOOTH_STEP);
    state->feedback[i] =
        gm2_job_smooth(state->feedback[i], config->feedback[i], GM2_JOB_SMOOTH_STEP);
  }
  for (unsigned i = 0; i < 2u * GM2_PITCH_CORE_VOICES; i++) {
    state->pan[i] = gm2_job_smooth(state->pan[i], config->pan[i], GM2_JOB_SMOOTH_STEP);
  }
  state->wet = gm2_job_smooth(state->wet, config->wet, GM2_JOB_SMOOTH_STEP);
  state->dry = gm2_job_smooth(state->dry, config->dry, GM2_JOB_SMOOTH_STEP);
}

/* One stereo frame, P:0x000D15, then the chunk's job run once its last frame is
 * done. */
static void pitch_frame(gm2_pitch_core_state *state, const gm2_pitch_core_config *config,
                        const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS], int depopped) {
  const int32_t left = gm2_s24((uint32_t)in[0]);
  const int32_t right = gm2_s24((uint32_t)in[1]);
  int limit = 0;

  state->chunk[state->chunk_at] = left;
  state->chunk[state->chunk_at + 1u] = right;
  state->chunk_at = (state->chunk_at + 2u) % (GM2_CHUNK_FRAMES * GM2_CHANNELS);

  /* The mono input and both voices' feedback into the buffer, which the write
   * index walks down. Each product is at most 2^47 and the sum under 2^49. */
  int64_t a = 2 * (int64_t)GM2_LEVEL_INPUT * left;
  a += 2 * (int64_t)right * GM2_LEVEL_INPUT;
  a = gm2_asr_n(a, 1);
  a += 2 * (int64_t)state->feedback[0] * state->voice[0].out;
  a += 2 * (int64_t)state->feedback[1] * state->voice[1].out;
  state->audio_buf[state->audio_at] = pitch_store(a, &limit);
  state->audio_at = state->audio_at == 0u ? GM2_PITCH_CORE_AUDIO_WORDS - 1u : state->audio_at - 1u;

  for (unsigned i = 0; i < GM2_PITCH_CORE_VOICES; i++) {
    pitch_voice(state, &state->voice[i], &limit, depopped);
  }

  /* The pan matrix, then each channel's wet and dry products, rounded together
   * once. Every operand is a 24-bit word, so each doubled product is at most 2^47
   * and their sum stays inside the 56-bit accumulator: no wrap can act. */
  const int32_t one = state->voice[0].out;
  const int32_t two = state->voice[1].out;
  const int32_t wet = state->wet;
  const int32_t dry = state->dry;
  int64_t right_sum = 2 * (int64_t)state->pan[0] * two;
  int64_t left_sum = 2 * (int64_t)two * state->pan[1];
  left_sum += 2 * (int64_t)one * state->pan[2];
  right_sum += 2 * (int64_t)state->pan[3] * one;
  const int64_t wet_l = 2 * (int64_t)wet * gm2_store24(left_sum);
  const int64_t wet_r = 2 * (int64_t)wet * gm2_store24(right_sum);
  out[0] = gm2_store24(gm2_round24(wet_l + 2 * (int64_t)left * dry));
  out[1] = gm2_store24(gm2_round24(wet_r + 2 * (int64_t)right * dry));

  if (state->chunk_pos == 0u) {
    pitch_chunk(state, config);
  }
  if (++state->chunk_pos < GM2_CHUNK_FRAMES) {
    return;
  }
  state->chunk_pos = 0;
  pitch_jobs(state, config);
}

void gm2_pitch_core_process_frame(gm2_pitch_core_state *state, const gm2_pitch_core_config *config,
                                  const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  pitch_frame(state, config, in, out, 0);
}

void gm2_pitch_core_process_frame_depopped(gm2_pitch_core_state *state,
                                           const gm2_pitch_core_config *config,
                                           const int32_t in[GM2_CHANNELS],
                                           int32_t out[GM2_CHANNELS]) {
  pitch_frame(state, config, in, out, 1);
}

void gm2_pitch_core_state_init(gm2_pitch_core_state *state, const gm2_pitch_core_config *config) {
  memset(state, 0, sizeof *state);
  for (unsigned i = 0; i < GM2_PITCH_CORE_RING_WORDS; i++) {
    state->period_ring[i] = PITCH_REST_PERIOD;
  }
  state->period = PITCH_REST_PERIOD;
  state->cycle = 1;
  state->coarse_state = PITCH_COARSE_SOLVE;
  state->peak_period = PITCH_REST_PEAK;
  state->tracked_period = PITCH_REST_PEAK;
  state->fine_state = PITCH_FINE_CAPTURE;
  state->fine_window = PITCH_REST_FINE_WINDOW;
  state->fine_estimate = PITCH_REST_FINE_ESTIMATE;
  state->fine_stride = PITCH_REST_FINE_STRIDE;

  for (unsigned i = 0; i < GM2_PITCH_CORE_VOICES; i++) {
    gm2_pitch_core_voice *voice = &state->voice[i];
    state->feedback[i] = config->feedback[i];
    state->ratio[i] = config->ratio[i];
    state->harmony[i] = config->ratio[i];
    voice->delay = config->delay[i];
    voice->span = PITCH_REST_PERIOD;
    pitch_window(voice, config);
  }
  for (unsigned i = 0; i < 2u * GM2_PITCH_CORE_VOICES; i++) {
    state->pan[i] = config->pan[i];
  }
  state->wet = config->wet;
  state->dry = config->dry;
  pitch_rates(state, config);
}
