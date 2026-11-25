/* The Pitch block's updaters on the firmware's software float. R is the sample
 * rate; the Tuner Ref is the fixed 440 Hz of X:0x099A61:
 *
 *   P:0x090A72  min_period = to_int(2^(hi/1200) * rate_khz * {0x7A4FED,0x81} * 256),
 *               max_period the same on lo,
 *               lag_count  = min(0x100, max_period / 4 + {0x7FDF3B,0x7F}),
 *               rate_factor = to_int({0x40A3D7,0x80} / min_period * 32768),
 *               where lo and hi are the Guitar range's {0xBB4000,0x8B} and
 *               {0x578000,0x8B}
 *   P:0x090B50  ref_offset = 0.0625 * the rate's pitch latency
 *               {0x79A55B,0x82} or {0x77B097,0x82}, with the Key over twelve
 *               added on the Intelligent Pitch Shifter
 *   P:0x091017  the harmony grid: for each note of the Scale's step pattern in
 *               gm2_pitch_scale_table, the intervals up the scale from it, and
 *               each gap between two scale notes filled from its two ends
 *   P:0x0910BD  one voice's curve: the grid row of its Voice value less the
 *               row's mean, three points a semitone, and the ratio of that mean
 *   P:0x090C89  ratio = (2^(cents/1200) - 1) in Q20, from the Voice values on the
 *               Detune and the Pitch Shifter, from Pitch, Direction and Range on
 *               the Whammy and the Octaver
 *   P:0x090D7F  delay = to_int(ms * rate_khz) in Q8 samples, a fixed word on the
 *               Whammy and the Octaver
 *   P:0x090E51  the feedback pair on the Pitch Shifter, the four pan gains from
 *               Pan and Level, and the fixed Detune, Whammy and Octaver routing
 *
 * Mix is not one of them: the level code P:0x08F763 stages the pair the output
 * stage multiplies the voice sum and the input by, at Out Level 0 dB. The Mix
 * callback P:0x091287 reaches P:0x0912C7 on the Whammy, which pins its mix word
 * at 0x7FFFFF.
 */
#include "gm2_pitch_design.h"

#include "gm2_dsp_math.h"
#include "gm2_mix.h"
#include "gm2_swf.h"
#include "gm2_tables.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  DETUNE_VOICE_MAX = 100, /* cents either way */
  PITCH_VOICE_MAX = 2400,
  IPITCH_VOICE_MAX = 12, /* scale steps either way */
  PAN_MIN = -50,
  PAN_MAX = 50,
  DELAY_MIN = 0,
  DETUNE_DELAY_MAX = 50,
  DELAY_MAX = 350,
  FEEDBACK_MIN = 0,
  FEEDBACK_MAX = 100,
  LEVEL_MIN = -100,
  LEVEL_MAX = 0,
  KEY_MIN = 0,
  KEY_MAX = 11,
  SCALE_MIN = 0,
  SCALE_MAX = (int)GM2_PITCH_SCALE_COUNT - 1,
  PITCH_MIN = 0,
  PITCH_MAX = 100,
  DIRECTION_MIN = 0,
  DIRECTION_MAX = 1,
  RANGE_MIN = 1,
  RANGE_MAX = 2,
};

/* X:0x0997BF..0x09981E: the harmony grid's eight rows of twelve semitones. */
enum {
  PITCH_DESIGN_GRID_ROWS = 8,
  PITCH_DESIGN_GRID_NOTES = 12,
  PITCH_DESIGN_GRID_WORDS = PITCH_DESIGN_GRID_ROWS * PITCH_DESIGN_GRID_NOTES,
  PITCH_DESIGN_LAG_MAX = 0x000100,
  PITCH_DESIGN_WHAMMY_DELAY = 0x037200, /* the delay P:0x090D7F gives the Whammy and the Octaver */
};

static const gm2_swf k_twelve = GM2_SWF_CONST(0x600000u, 0x83u); /* 12 */
static const gm2_swf k_rate_cents_low = GM2_SWF_CONST(0xBB4000u, 0x8Bu); /* -2200 */
static const gm2_swf k_rate_cents_high = GM2_SWF_CONST(0x578000u, 0x8Bu); /* 2800 */
static const gm2_swf k_octave_cents = GM2_SWF_CONST(0x4B0000u, 0x8Au); /* 1200 */
static const gm2_swf k_log2_per_cent = GM2_SWF_CONST(0xB44A33u, 0x75u); /* -ln 2 / 1200 */
static const gm2_swf k_log2_per_1200 = GM2_SWF_CONST(0x4BB5CDu, 0x75u); /* ln 2 / 1200 */
static const gm2_swf k_1200_inverse = GM2_SWF_CONST(0x6D3A07u, 0x75u); /* 1/1200 */
static const gm2_swf k_period_scale = GM2_SWF_CONST(0x7A4FEDu, 0x81u); /* 3.822257 */
static const gm2_swf k_lag_bias = GM2_SWF_CONST(0x7FDF3Bu, 0x7Fu); /* 0.999 */
static const gm2_swf k_samples_q8 = GM2_SWF_CONST(0x400000u, 0x88u); /* 256 */
static const gm2_swf k_crossfade_scale = GM2_SWF_CONST(0x40A3D7u, 0x80u); /* 1.01 */
static const gm2_swf k_q15 = GM2_SWF_CONST(0x400000u, 0x8Fu); /* 32768 */
static const gm2_swf k_rate_split = GM2_SWF_CONST(0x59D800u, 0x8Fu); /* 46016 Hz */
static const gm2_swf k_pitch_latency_slow = GM2_SWF_CONST(0x79A55Bu, 0x82u); /* 7.60287 */
static const gm2_swf k_pitch_latency_fast = GM2_SWF_CONST(0x77B097u, 0x82u); /* 7.48061 */
static const gm2_swf k_q23 = GM2_SWF_CONST(0x7FFFFFu, 0x96u); /* 8388607 */
static const gm2_swf k_one_q23 = GM2_SWF_CONST(0x7FFFFFu, 0x7Fu); /* 1 - 2^-23 */
static const gm2_swf k_sixteenth = GM2_SWF_CONST(0x400000u, 0x7Cu); /* 1/16 */
static const gm2_swf k_eighth = GM2_SWF_CONST(0x400000u, 0x7Du); /* 1/8 */
static const gm2_swf k_third_semitone = GM2_SWF_CONST(0x42AAABu, 0x85u); /* 100/3 cents */
static const gm2_swf k_cent = GM2_SWF_CONST(0x51EB84u, 0x79u); /* 0.01 */

/* The eight rows of column `src` copied into column `dst`, columns modulo 12. */
static void pitch_grid_copy(int32_t grid[PITCH_DESIGN_GRID_WORDS], int src, int dst) {
  const int from = src % PITCH_DESIGN_GRID_NOTES;
  const int to = dst % PITCH_DESIGN_GRID_NOTES;
  for (int row = 0; row < PITCH_DESIGN_GRID_ROWS; row++) {
    grid[PITCH_DESIGN_GRID_NOTES * row + to] = grid[PITCH_DESIGN_GRID_NOTES * row + from];
  }
}

/* The mean of two columns into a third. */
static void pitch_grid_average(int32_t grid[PITCH_DESIGN_GRID_WORDS], int first, int second,
                               int dst) {
  const int a = first % PITCH_DESIGN_GRID_NOTES;
  const int b = second % PITCH_DESIGN_GRID_NOTES;
  const int to = dst % PITCH_DESIGN_GRID_NOTES;
  for (int row = 0; row < PITCH_DESIGN_GRID_ROWS; row++) {
    grid[PITCH_DESIGN_GRID_NOTES * row + to] =
        (grid[PITCH_DESIGN_GRID_NOTES * row + a] + grid[PITCH_DESIGN_GRID_NOTES * row + b]) / 2;
  }
}

/* P:0x091017: the harmony grid of `scale`, returning the scale's note count. */
static int pitch_harmony_grid(int scale, int32_t grid[PITCH_DESIGN_GRID_WORDS]) {
  const int32_t *table = gm2_pitch_scale_table[scale];
  const int notes = (int)table[0];
  int32_t steps[GM2_PITCH_SCALE_STRIDE] = {0};
  int remaining = PITCH_DESIGN_GRID_NOTES;
  for (int i = 0; i < notes - 1; i++) {
    steps[i] = table[1 + i];
    remaining -= table[1 + i];
  }
  steps[notes - 1] = remaining;

  int position = 0;
  for (int degree = 0; degree < notes; degree++) {
    int32_t cents = 0;
    for (int note = 0; note < notes; note++) {
      grid[PITCH_DESIGN_GRID_NOTES * note + position] = 100 * cents;
      cents += steps[(degree + note) % notes];
    }
    position += steps[degree];
  }

  int start = 0;
  for (int i = 0; i < notes; i++) {
    const int span = (int)steps[i];
    if (span > 1) {
      int gap = span - 1;
      int offset = 1;
      while (gap > 1) {
        pitch_grid_copy(grid, start, start + offset);
        pitch_grid_copy(grid, start + span, start + span - offset);
        gap -= 2;
        offset += 1;
      }
      if (gap != 0) {
        pitch_grid_average(grid, start, start + span, start + offset);
      }
    }
    start += span;
  }
  return notes;
}

/* P:0x090B50 and P:0x0910BD: ref_offset X:0x0424 for one Key value. */
static int32_t pitch_ref_offset(gm2_swf tuning, int key) {
  const gm2_swf octaves = gm2_swf_add(gm2_swf_div(gm2_swf_from_int(key), k_twelve), tuning);
  return gm2_swf_to_int(gm2_swf_mul(gm2_swf_mul(octaves, k_sixteenth), k_q23));
}

/* P:0x0910BD: one voice's harmony curve and the ratio of its mean, from the grid
 * of a scale of `notes` notes. */
static void pitch_voice_curve(unsigned voice, int steps,
                              const int32_t grid[PITCH_DESIGN_GRID_WORDS], int notes,
                              gm2_pitch_core_config *config) {
  const int32_t index = gm2_clamp_raw(steps, -IPITCH_VOICE_MAX, IPITCH_VOICE_MAX) + 100 * notes;
  const int32_t *row = &grid[PITCH_DESIGN_GRID_NOTES * (index % notes)];

  gm2_swf sum = gm2_swf_zero;
  for (int note = 0; note < PITCH_DESIGN_GRID_NOTES; note++) {
    sum = gm2_swf_add(gm2_swf_from_int(row[note]), sum);
  }
  const gm2_swf average = gm2_swf_div(sum, k_twelve);

  gm2_swf points[GM2_PITCH_CORE_CURVE_WORDS];
  const gm2_swf base = gm2_swf_sub(gm2_swf_from_int(row[0]), average);
  points[0] = base;
  points[1] = gm2_swf_sub(base, k_third_semitone);
  points[35] = gm2_swf_add(base, k_third_semitone);
  points[36] = base;
  for (int note = 1; note < PITCH_DESIGN_GRID_NOTES; note++) {
    const gm2_swf value = gm2_swf_sub(gm2_swf_from_int(row[note]), average);
    points[3 * note] = value;
    points[3 * note + 1] = gm2_swf_sub(value, k_third_semitone);
    points[3 * note - 1] = gm2_swf_add(value, k_third_semitone);
  }

  const gm2_swf cents = gm2_swf_mac(average, gm2_swf_from_int(index / notes - 100), k_octave_cents);
  const gm2_swf ratio = gm2_swf_exp(gm2_swf_mul(cents, k_log2_per_1200));

  config->curve_gain[voice] = gm2_swf_to_int(gm2_swf_mul(gm2_swf_mul(ratio, k_eighth), k_q23));
  for (unsigned i = 0; i < GM2_PITCH_CORE_CURVE_WORDS; i++) {
    config->curve[voice][i] =
        gm2_swf_to_int(gm2_swf_mul(gm2_swf_mul(points[i], k_1200_inverse), k_q23));
  }
}

/* P:0x090A72: the detector's period window and the crossfade rate factor. */
static void pitch_period_window(const gm2_rate_words *rate, gm2_pitch_core_config *config) {
  const gm2_swf low = k_rate_cents_low;
  const gm2_swf high = k_rate_cents_high;

  const gm2_swf scale = gm2_swf_mul(rate->rate_khz, k_period_scale);
  const gm2_swf shortest = gm2_swf_mul(gm2_swf_exp(gm2_swf_mul(high, k_log2_per_cent)), scale);
  const gm2_swf longest = gm2_swf_mul(gm2_swf_exp(gm2_swf_mul(low, k_log2_per_cent)), scale);

  const int32_t lags = gm2_swf_to_int(gm2_swf_add(gm2_swf_div(longest, gm2_swf_four), k_lag_bias));
  config->min_period = gm2_swf_to_int(gm2_swf_mul(shortest, k_samples_q8));
  config->max_period = gm2_swf_to_int(gm2_swf_mul(longest, k_samples_q8));
  config->lag_count = lags > PITCH_DESIGN_LAG_MAX ? PITCH_DESIGN_LAG_MAX : lags;
  const gm2_swf step = gm2_swf_div(k_crossfade_scale, shortest);
  config->rate_factor = gm2_swf_to_int(gm2_swf_mul(step, k_q15));
}

/* P:0x090B50: the rate's pitch latency; the Tuner Ref is fixed at 440 Hz. */
static gm2_swf pitch_tuning(const gm2_rate_words *rate) {
  return gm2_swf_cmp(rate->rate, k_rate_split) < 0 ? k_pitch_latency_slow : k_pitch_latency_fast;
}

/* P:0x090C89: (2^(cents/1200) - 1) in Q20, the kernel's pitch ratio word. */
static int32_t pitch_ratio_word(gm2_swf cents) {
  const gm2_swf ratio = gm2_swf_pow(gm2_swf_two, gm2_swf_div(cents, k_octave_cents));
  const gm2_swf delta = gm2_swf_mul(gm2_swf_sub(ratio, gm2_swf_one), k_samples_q8);
  return gm2_s24((uint32_t)gm2_swf_to_int(delta) << 12);
}

/* P:0x090C89: the two pitch ratios of the Detune, the Whammy, the Octaver and the
 * Pitch Shifter. The Whammy and the Octaver drive their one voice from Direction
 * and Range, the Whammy by its Pitch share of them. */
static void pitch_ratios(gm2_pitch_design_subtype subtype, const gm2_pitch_design_raw *raw,
                         gm2_pitch_core_config *config) {
  if (subtype == GM2_PITCH_DESIGN_WHAMMY || subtype == GM2_PITCH_DESIGN_OCTAVER) {
    const gm2_swf direction = gm2_swf_sub(
        gm2_swf_mul(gm2_swf_from_int(gm2_clamp_raw(raw->direction, DIRECTION_MIN, DIRECTION_MAX)),
                    gm2_swf_two),
        gm2_swf_one);
    const gm2_swf range =
        gm2_swf_mul(gm2_swf_from_int(gm2_clamp_raw(raw->range, RANGE_MIN, RANGE_MAX)),
                    subtype == GM2_PITCH_DESIGN_WHAMMY ? k_twelve : k_octave_cents);
    gm2_swf cents = gm2_swf_mul(range, direction);
    if (subtype == GM2_PITCH_DESIGN_WHAMMY) {
      cents = gm2_swf_mul(gm2_swf_from_int(gm2_clamp_raw(raw->pitch, PITCH_MIN, PITCH_MAX)), cents);
    }
    config->ratio[0] = pitch_ratio_word(cents);
    return;
  }
  const int limit = subtype == GM2_PITCH_DESIGN_DETUNE ? DETUNE_VOICE_MAX : PITCH_VOICE_MAX;
  for (unsigned voice = 0; voice < GM2_PITCH_CORE_VOICES; voice++) {
    config->ratio[voice] =
        pitch_ratio_word(gm2_swf_from_int(gm2_clamp_raw(raw->voice[voice], -limit, limit)));
  }
}

/* P:0x090D7F: the two voice delays in Q8 samples. */
static void pitch_delays(const gm2_rate_words *rate, gm2_pitch_design_subtype subtype,
                         const gm2_pitch_design_raw *raw, gm2_pitch_core_config *config) {
  if (subtype == GM2_PITCH_DESIGN_WHAMMY || subtype == GM2_PITCH_DESIGN_OCTAVER) {
    config->delay[0] = PITCH_DESIGN_WHAMMY_DELAY;
    config->delay[1] = PITCH_DESIGN_WHAMMY_DELAY;
    return;
  }
  const int longest = subtype == GM2_PITCH_DESIGN_DETUNE ? DETUNE_DELAY_MAX : DELAY_MAX;
  for (unsigned voice = 0; voice < GM2_PITCH_CORE_VOICES; voice++) {
    const gm2_swf samples = gm2_swf_mul(
        gm2_swf_from_int(gm2_clamp_raw(raw->delay[voice], DELAY_MIN, longest)), rate->rate_khz);
    config->delay[voice] = gm2_s24((uint32_t)gm2_swf_to_int(samples) << 8);
  }
}

/* P:0x090E51: the feedback pair and the four pan gains. */
static void pitch_levels(gm2_pitch_design_subtype subtype, const gm2_pitch_design_raw *raw,
                         gm2_pitch_core_config *config) {
  if (subtype == GM2_PITCH_DESIGN_PITCH) {
    int feedback[GM2_PITCH_CORE_VOICES];
    for (unsigned voice = 0; voice < GM2_PITCH_CORE_VOICES; voice++) {
      feedback[voice] = gm2_clamp_raw(raw->feedback[voice], FEEDBACK_MIN, FEEDBACK_MAX);
    }
    if (feedback[0] + feedback[1] > FEEDBACK_MAX) {
      feedback[0] = FEEDBACK_MAX - feedback[1];
    }
    for (unsigned voice = 0; voice < GM2_PITCH_CORE_VOICES; voice++) {
      config->feedback[voice] =
          gm2_swf_to_q23(gm2_swf_mul(gm2_swf_from_int(feedback[voice]), k_cent));
    }
  }

  if (subtype == GM2_PITCH_DESIGN_DETUNE) {
    config->pan[0] = GM2_W24_MAX;
    config->pan[2] = GM2_W24_MAX;
    return;
  }
  if (subtype == GM2_PITCH_DESIGN_WHAMMY || subtype == GM2_PITCH_DESIGN_OCTAVER) {
    config->pan[2] = GM2_W24_MAX;
    config->pan[3] = GM2_W24_MAX;
    return;
  }
  for (unsigned voice = 0; voice < GM2_PITCH_CORE_VOICES; voice++) {
    const gm2_swf pan = gm2_swf_mul(
        gm2_swf_from_int(gm2_clamp_raw(raw->pan[voice], PAN_MIN, PAN_MAX) - PAN_MIN), k_cent);
    const gm2_swf level = gm2_swf_db(gm2_clamp_raw(raw->level[voice], LEVEL_MIN, LEVEL_MAX));
    config->pan[3u - 3u * voice] = gm2_swf_to_q23(gm2_swf_mul(level, pan));
    config->pan[2u - voice] = gm2_swf_to_q23(gm2_swf_mul(level, gm2_swf_sub(k_one_q23, pan)));
  }
}

void gm2_pitch_design(const gm2_rate_words *rate, gm2_pitch_design_subtype subtype,
                      const gm2_pitch_design_raw *raw, gm2_pitch_core_config *config) {
  *config = (gm2_pitch_core_config){0};
  pitch_period_window(rate, config);
  const gm2_swf tuning = pitch_tuning(rate);

  if (subtype == GM2_PITCH_DESIGN_IPITCH) {
    int32_t grid[PITCH_DESIGN_GRID_WORDS] = {0};
    const int notes = pitch_harmony_grid(gm2_clamp_raw(raw->scale, SCALE_MIN, SCALE_MAX), grid);
    for (unsigned voice = 0; voice < GM2_PITCH_CORE_VOICES; voice++) {
      pitch_voice_curve(voice, raw->voice[voice], grid, notes, config);
    }
    config->harmonize = 1;
    config->ref_offset = pitch_ref_offset(tuning, gm2_clamp_raw(raw->key, KEY_MIN, KEY_MAX));
  } else {
    pitch_ratios(subtype, raw, config);
    config->ref_offset = pitch_ref_offset(tuning, 0);
  }
  pitch_delays(rate, subtype, raw, config);
  pitch_levels(subtype, raw, config);
  gm2_mix_words(subtype == GM2_PITCH_DESIGN_WHAMMY ? 100 : raw->mix, &config->wet, &config->dry);
}
