/* Parameter setup for the Reverb block, algorithm 5: P:0x095099 and the routines
 * it runs, on the firmware's software float.
 *
 * Spring, Hall, Room and Plate are subtypes 0..3 of one algorithm and run one
 * setup chain; switching Subtype only swaps the four parameter banks P:0x0951AD
 * stages at UI+3..58 from the pointers at X:0x0992EC..0x0992EF. With Dec the
 * Decay, Pre the Pre-Delay, Sh/Sz/HiC/HiF/LoC/LoF the Shape, Size, Hi Color,
 * Hi Factor, Lo Color and Lo Factor, RL/Rv the Room and Reverb levels and Df the
 * Diffuse value, all clamped by P:0x0832CB into their descriptor windows (the
 * enum below), the routines are
 *
 *   P:0x09520C  the Room and Reverb levels: g1 = dB(RL + Size[5 Sz + 45 Sh + 3] - 14),
 *               g2 = dB(Rv - 5), the Room words = g1, the Reverb words = g2
 *               below 0.5 and 1.0 from 0.5 on, the pre-delay word = max(0.5, g2)
 *   P:0x0952D4  the tank's damping bands: c_l = HiF(exp(-2 pi LoC / rate)) and likewise
 *               c_h, HiF taking the low fifteen bits, the decay scaled by the Lo and Hi
 *               multipliers (P:0x08F179, P:0x08F161, mul48 and div48), then seven
 *               six-word filters {c_l, 1 - c_l, c_h, r1, g_h, g_l - g_m} each
 *               shifted up eight bits, where r1 = P:0x08F179(1 - c_h, g_m - g_h)
 *   P:0x095601  the pointers' offsets: the diffuser and tank tap offsets from the seven
 *               tank lengths, the rate / 10 and the constants 0x0A4C50, 0x003C0F
 *               and 0x001BA9
 *   P:0x095730  the early reflections' offsets: the eight left and eight right early tap
 *               offsets from the geometry table, the clamped Pre and the rate / 100,
 *               railed at 0x0033AE samples, then the pre-delay tap. The routine ends by
 *               running P:0x0954A4.
 *   P:0x0954A4  the two diffusers: g = to_q23((min(55, max(1, Df + b)) / 100))^(1/3) with b
 *               the two-piece fit 6.72721 Dec / 10 below 71 and
 *               1.80187 Dec / 10 + 35 from 71 on, and the complements = 1 - g * g; it also
 *               scales Dec by the larger Lo/Hi multiplier when either passes 100 %
 *   P:0x09526A  the input one-pole: the input low-pass one-pole at the Hi Color frequency
 *               scaled by 1 + HiF * 0.01 (HiF negative counted twice) and its gain
 *   P:0x095973  the early reflections' weights and comb feedbacks: the eight early tap
 *               weights of each channel, each a one-pole low-pass at the Hi Color-scaled
 *               geometry frequency with its dB gain, signed by the tap's table word, and
 *               the three comb feedbacks of each channel
 *   P:0x095A71  the early reflections' biquads: two early reflection biquads, both channels
 *               running the same pair, from the Hi Color and Shape descriptors through
 *               P:0x08F233 and staged as {b0, b2, b1, a2, a1}
 *   P:0x095710  the tank's signs: the fourteen tank feedback matrix signs
 *
 * Mix is not one of them: the level code P:0x08F763 stages the input, wet and
 * dry words at X:0x099046..0x099048 from the level struct at UI+63, at Out Level
 * 0 dB.
 */
#include "gm2_reverb_setup.h"

#include "gm2_dsp_math.h"
#include "gm2_mix.h"
#include "gm2_rate.h"
#include "gm2_swf.h"
#include "gm2_tables.h"

#include <stddef.h>
#include <stdint.h>

/* The firmware's operands as (mantissa, exponent) words. */
static const gm2_swf k_ten = GM2_SWF_CONST(0x500000u, 0x83u);       /* 10 */
static const gm2_swf k_twenty = GM2_SWF_CONST(0x500000u, 0x84u);    /* 20 */
static const gm2_swf k_fifty = GM2_SWF_CONST(0x640000u, 0x85u);     /* 50 */
static const gm2_swf k_hundred = GM2_SWF_CONST(0x640000u, 0x86u);   /* 100 */
static const gm2_swf k_cent = GM2_SWF_CONST(0x51EB85u, 0x79u);      /* 0.01 */
static const gm2_swf k_rail15 = GM2_SWF_CONST(0x7FFF00u, 0x8Eu); /* 32767.4, the fifteen-bit rail */
static const gm2_swf k_db_step = GM2_SWF_CONST(0x5E504Du, 0x79u); /* ln(10) / 200 */
static const gm2_swf k_diff_low = GM2_SWF_CONST(0x6BA29Du, 0x82u); /* 6.72721 */
static const gm2_swf k_diff_high = GM2_SWF_CONST(0x7350B5u, 0x80u); /* 1.80187 */
static const gm2_swf k_diff_bias = GM2_SWF_CONST(0x460000u, 0x85u); /* 35 */
static const gm2_swf k_diff_max = GM2_SWF_CONST(0x6E0000u, 0x85u); /* 55 */
static const gm2_swf k_cube_root = GM2_SWF_CONST(0x555326u, 0x7Eu); /* 1/3 */

/* The offset P:0x095730 rails its taps at, the gap below which an odd tap
 * interpolates toward its predecessor instead of scaling its own delay, and the
 * top of the tank buffer every offset counts back from. */
enum {
  REVERB_SETUP_TAP_LIMIT = 0x0033AE,
  REVERB_SETUP_TAP_INTERPOLATES = 10,
  REVERB_SETUP_TANK_TOP = 0x0A7FFF,
};

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  DECAY_MIN = 1,
  DECAY_MAX = 200,
  PRE_DELAY_MIN = 0,
  PRE_DELAY_MAX = 100,
  SHAPE_MIN = 0,
  SHAPE_MAX = 2,
  SIZE_MIN = 0,
  EFFECT_SIZE_MAX = 7,
  HI_COLOR_MIN = 0,
  HI_COLOR_MAX = 6,
  HI_FACTOR_MIN = -25,
  HI_FACTOR_MAX = 25,
  LO_COLOR_MIN = 0,
  LO_COLOR_MAX = 6,
  LO_FACTOR_MIN = -25,
  LO_FACTOR_MAX = 25,
  ROOM_LEVEL_MIN = -100,
  ROOM_LEVEL_MAX = 0,
  REVERB_LEVEL_MIN = -100,
  REVERB_LEVEL_MAX = 0,
  DIFFUSE_MIN = -25,
  DIFFUSE_MAX = 25,
};

/* P:0x08F1FE: the biquad's alpha, sin(w0) * sinh(bw * limit / 2) / sqrt(gain). */
static gm2_swf reverb_biquad_alpha(gm2_swf bw, gm2_swf gain_sqrt, gm2_swf w0) {
  gm2_swf sin_w0 = gm2_swf_sin(w0);
  gm2_swf limit = gm2_swf_div(w0, sin_w0);
  gm2_swf a = gm2_swf_mul(limit, gm2_swf_mul(gm2_swf_half, bw));
  a = gm2_swf_sinh(a);
  return gm2_swf_div(gm2_swf_mul(a, sin_w0), gain_sqrt);
}

/* P:0x08F179 */
static int32_t reverb_scale_product(int32_t a, int32_t b) {
  return gm2_lo24(gm2_asr_n(gm2_asr_n(gm2_mpy(b, a), 1u), 15u));
}

static void reverb_biquad_design(gm2_swf two_pi_over_rate, const int32_t desc[4], int32_t out[5]) {
  /* P:0x08F233 */
  const gm2_swf gain = gm2_swf_div(gm2_swf_from_int(desc[3]), k_ten);
  const gm2_swf ratio = gm2_swf_pow(k_ten, gm2_swf_div(gain, k_twenty));
  const gm2_swf gain_sqrt = gm2_swf_sqrt(ratio);
  const gm2_swf freq = gm2_swf_from_words((uint32_t)desc[1], (uint32_t)desc[0]);
  const gm2_swf w0 = gm2_swf_mul(freq, two_pi_over_rate);
  const gm2_swf bw = gm2_swf_mul(gm2_swf_ln(gm2_swf_two), gm2_biquad_q_table[desc[2]]);
  const gm2_swf alpha = reverb_biquad_alpha(bw, gain_sqrt, w0);
  const gm2_swf peak = gm2_swf_mul(ratio, alpha);
  const gm2_swf pole = gm2_swf_div(gm2_swf_one, gm2_swf_add(gm2_swf_one, alpha));
  const gm2_swf cos_w0 = gm2_swf_mul(gm2_swf_mul(gm2_swf_cos(w0), gm2_swf_minus_two), pole);
  const gm2_swf feed = gm2_swf_mul(gm2_swf_sub(gm2_swf_one, alpha), pole);
  out[0] =
      gm2_swf_to_q23(gm2_swf_mul(gm2_swf_mul(gm2_swf_add(gm2_swf_one, peak), pole), gm2_swf_half));
  out[1] = gm2_swf_to_q23(gm2_swf_mul(cos_w0, gm2_swf_half));
  out[2] =
      gm2_swf_to_q23(gm2_swf_mul(gm2_swf_mul(gm2_swf_sub(gm2_swf_one, peak), pole), gm2_swf_half));
  out[4] = gm2_swf_to_q23(gm2_swf_mul(gm2_swf_neg(cos_w0), gm2_swf_half));
  out[3] = gm2_swf_to_q23(gm2_swf_mul(gm2_swf_neg(feed), gm2_swf_half));
}

/* P:0x09520C: the room and reverb level gains and the diffuse tail multiplier. */
static void reverb_levels(int room_level, int reverb_level, int shape, int size,
                          gm2_reverb_config *config) {
  const int idx = 5 * size + 45 * shape;
  const int room_db = gm2_s24((uint32_t)(room_level + gm2_reverb_size_table[idx + 3])) - 14;
  const int32_t room = gm2_swf_db_to_lin(room_db);
  const int32_t reverb = gm2_swf_db_to_lin(reverb_level - 5);
  config->coef[GM2_REVERB_ROOM] = room;
  config->coef[GM2_REVERB_ROOM + 1] = room;
  config->coef[GM2_REVERB_LEVEL] = reverb >= 0x400000 ? GM2_W24_MAX : reverb;
  config->coef[GM2_REVERB_LEVEL + 1] = config->coef[GM2_REVERB_LEVEL];
  config->coef[GM2_REVERB_PRE_DELAY] = reverb >= 0x400000 ? reverb : 0x400000;
}

/* The Lo and Hi decay multipliers P:0x0952D4 and P:0x0954A4 share: a positive
 * factor moves the table multiplier up by factor/50, the rest by factor/100. */
static gm2_swf reverb_color_multiplier(int factor, int32_t multiplier) {
  const gm2_swf divisor = factor > 0 ? k_fifty : k_hundred;
  const gm2_swf scale = gm2_swf_add(gm2_swf_div(gm2_swf_from_int(factor), divisor), gm2_swf_one);
  return gm2_swf_mul(scale, gm2_swf_from_int(multiplier));
}

/* The one-pole cutoff P:0x0952D4 takes from a frequency in Hz: the low fifteen
 * bits of exp(-2 pi f / rate), which is what its damping filters scale by. */
static int32_t reverb_one_pole(int32_t hz, gm2_swf minus_omega) {
  return gm2_swf_to_int(
      gm2_swf_mul(gm2_swf_exp(gm2_swf_mul(gm2_swf_from_int(hz), minus_omega)), k_rail15));
}

/* P:0x0952D4: the seven tank damping filters. */
static void reverb_damping(const gm2_rate_words *rate, int decay, int hi_color, int hi_factor,
                           int lo_color, int lo_factor, gm2_reverb_config *config) {
  const int32_t c_lo = reverb_one_pole(gm2_reverb_locol_table[2 * lo_color], rate->minus_omega);
  const int32_t c_hi = reverb_one_pole(gm2_reverb_hicol_table[4 * hi_color], rate->minus_omega);
  const int32_t one_m_c_lo = gm2_s24((uint32_t)((0x7FFF - c_lo) + 1));
  const int32_t one_m_c_hi = gm2_s24((uint32_t)((0x7FFF - c_hi) + 1));

  const int64_t decay_word = gm2_sext48(decay);
  const int32_t lo_scaled =
      gm2_swf_to_int(reverb_color_multiplier(lo_factor, gm2_reverb_locol_table[2 * lo_color + 1]));
  const int32_t hi_scaled =
      gm2_swf_to_int(reverb_color_multiplier(hi_factor, gm2_reverb_hicol_table[4 * hi_color + 1]));
  int64_t decay_lo = gm2_div48(gm2_mul48(decay_word, gm2_sext48(lo_scaled)), 100);
  int64_t decay_hi = gm2_div48(gm2_mul48(decay_word, gm2_sext48(hi_scaled)), 100);
  int64_t decay_mid = decay_word;
  if (decay_lo < 2) {
    decay_lo = 2;
  }
  if (decay_mid < 2) {
    decay_mid = 2;
  }
  if (decay_hi < 2) {
    decay_hi = 2;
  }

  for (unsigned k = 0; k < GM2_REVERB_TANK_LINES; k++) {
    const int64_t product = gm2_mul48(gm2_sext48(gm2_reverb_tank_lengths[k]), gm2_sext48(-3));
    const int32_t g_lo = gm2_reverb_decay_interp(gm2_lo24(gm2_div48(product, decay_lo)));
    const int32_t g_mid = gm2_reverb_decay_interp(gm2_lo24(gm2_div48(product, decay_mid)));
    const int32_t g_hi = gm2_reverb_decay_interp(gm2_lo24(gm2_div48(product, decay_hi)));
    const int32_t rolloff = reverb_scale_product(one_m_c_hi, gm2_s24((uint32_t)(g_mid - g_hi)));
    const int32_t tilt = gm2_s24((uint32_t)(g_lo - g_mid));
    config->coef[GM2_REVERB_DAMPING + 6u * k] = gm2_s24((uint32_t)c_lo << 8);
    config->coef[GM2_REVERB_DAMPING + 6u * k + 1u] = gm2_s24((uint32_t)one_m_c_lo << 8);
    config->coef[GM2_REVERB_DAMPING + 6u * k + 2u] = gm2_s24((uint32_t)c_hi << 8);
    config->coef[GM2_REVERB_DAMPING + 6u * k + 3u] = gm2_s24((uint32_t)rolloff << 8);
    config->coef[GM2_REVERB_DAMPING + 6u * k + 4u] = gm2_s24((uint32_t)g_hi << 8);
    config->coef[GM2_REVERB_DAMPING + 6u * k + 5u] = gm2_s24((uint32_t)tilt << 8);
  }
}

/* P:0x095601: the diffuser and tank tap offset deltas. */
static void reverb_tank_offsets(const gm2_rate_words *rate, gm2_reverb_config *config) {
  /* The firmware walks down from the fixed position 0x0A4C50 through two gaps of
   * 0x003C0F and 0x001BA9 rate-scaled samples, one sample apart. */
  const int64_t rate_div10 = gm2_div48(gm2_swf_to_int48(rate->rate), 10);
  const int32_t base = 0x0A4C50;
  const int32_t gap_a = gm2_lo24(gm2_asr_n(gm2_mul48(rate_div10, 0x003C0F), 15u));
  const int32_t gap_b = gm2_lo24(gm2_asr_n(gm2_mul48(rate_div10, 0x001BA9), 15u));
  const int32_t pos_a = gm2_s24((uint32_t)(base - gap_a));
  const int32_t pos_b = gm2_s24((uint32_t)(pos_a - 1));
  const int32_t pos_c = gm2_s24((uint32_t)(pos_b - gap_b));
  int32_t pos = gm2_s24((uint32_t)(pos_c - 1));
  int32_t primary[7];
  int32_t tap_a[7];
  int32_t tap_b[7];

  for (unsigned k = 0; k < GM2_REVERB_TANK_LINES; k++) {
    const int32_t length_a = gm2_reverb_tank_lengths[k];
    const int32_t length_b = gm2_reverb_tank_lengths[7u + k];
    const int32_t scaled_a = gm2_lo24(gm2_asr_n(gm2_mul48(rate_div10, gm2_sext48(length_a)), 15u));
    const int32_t scaled_b = gm2_lo24(gm2_asr_n(gm2_mul48(rate_div10, gm2_sext48(length_b)), 15u));
    primary[k] = pos;
    tap_a[k] = gm2_s24((uint32_t)(pos - scaled_a));
    tap_b[k] = gm2_s24((uint32_t)(pos - scaled_b));
    pos = gm2_s24((uint32_t)((pos - (length_a > length_b ? scaled_a : scaled_b)) - 1));
  }

  config->offsets[1] = gm2_s24((uint32_t)(base - REVERB_SETUP_TANK_TOP));
  config->offsets[3] = gm2_s24((uint32_t)(pos_b - base));
  config->offsets[5] = gm2_s24((uint32_t)(primary[0] - pos_b));
  config->offsets[8] = gm2_s24((uint32_t)(primary[1] - primary[0]));
  config->offsets[10] = gm2_s24((uint32_t)(primary[2] - primary[1]));
  config->offsets[12] = gm2_s24((uint32_t)(primary[3] - primary[2]));
  config->offsets[14] = gm2_s24((uint32_t)(primary[4] - primary[3]));
  config->offsets[16] = gm2_s24((uint32_t)(primary[5] - primary[4]));
  config->offsets[18] = gm2_s24((uint32_t)(primary[6] - primary[5]));
  config->offsets[0] = gm2_s24((uint32_t)(pos_a - REVERB_SETUP_TANK_TOP));
  config->offsets[2] = gm2_s24((uint32_t)(pos_c - pos_a));
  config->offsets[4] = gm2_s24((uint32_t)(tap_a[0] - pos_c));
  config->offsets[6] = gm2_s24((uint32_t)(tap_a[1] - tap_a[0]));
  config->offsets[7] = gm2_s24((uint32_t)(tap_a[2] - tap_a[1]));
  config->offsets[9] = gm2_s24((uint32_t)(tap_a[3] - tap_a[2]));
  config->offsets[11] = gm2_s24((uint32_t)(tap_a[4] - tap_a[3]));
  config->offsets[13] = gm2_s24((uint32_t)(tap_a[5] - tap_a[4]));
  config->offsets[15] = gm2_s24((uint32_t)(tap_a[6] - tap_a[5]));
  config->offsets[17] = gm2_s24((uint32_t)(tap_b[0] - tap_a[6]));
  config->offsets[19] = gm2_s24((uint32_t)(tap_b[1] - tap_b[0]));
  config->offsets[21] = gm2_s24((uint32_t)(tap_b[2] - tap_b[1]));
  config->offsets[22] = gm2_s24((uint32_t)(tap_b[3] - tap_b[2]));
  config->offsets[23] = gm2_s24((uint32_t)(tap_b[4] - tap_b[3]));
  config->offsets[24] = gm2_s24((uint32_t)(tap_b[5] - tap_b[4]));
  config->offsets[25] = gm2_s24((uint32_t)(tap_b[6] - tap_b[5]));
}

/* The eight tap offsets of one channel at P:0x095730: an even tap scales its own
 * delay, an odd one whose delay is within REVERB_SETUP_TAP_INTERPOLATES of its predecessor's
 * splits the two, and the pre-delay shifts every tap by the same amount. The
 * table's fifth word of each entry is the slot the tap belongs to, a permutation
 * of 0..7 in every Shape. */
static void reverb_early_taps(const int32_t *table, int64_t rate_div100, int64_t scale,
                              int64_t pre_delay, int32_t taps[GM2_REVERB_EARLY_TAPS]) {
  for (unsigned k = 0; k < GM2_REVERB_EARLY_TAPS; k++) {
    const int64_t own = gm2_sext48(table[5u * k]);
    int64_t delay = gm2_div48(gm2_mul48(rate_div100, gm2_mul48(scale, own)), 10000);
    if ((k & 1u) != 0u) {
      const int64_t previous = gm2_sext48(table[5u * k - 5u]);
      if (own - previous <= REVERB_SETUP_TAP_INTERPOLATES) {
        delay = gm2_div48(gm2_mul48(rate_div100, gm2_mul48(scale, previous)), 10000);
        delay += gm2_div48(gm2_mul48(rate_div100, own - previous), 100);
      }
    }
    const int64_t total = delay + pre_delay;
    taps[table[5u * k + 4u]] = gm2_lo24(
        REVERB_SETUP_TANK_TOP - (total > REVERB_SETUP_TAP_LIMIT ? REVERB_SETUP_TAP_LIMIT : total));
  }
}

/* P:0x095730: the early reflection tap offsets. */
static void reverb_early_offsets(const gm2_rate_words *rate, int shape, int size, int pre_delay,
                                 gm2_reverb_config *config) {
  const int idx = 5 * size + 45 * shape;
  const int64_t rate_div100 = gm2_div48(gm2_swf_to_int48(rate->rate), 100);
  const int64_t scale = gm2_sext48(gm2_reverb_size_table[idx]);
  int64_t pre = gm2_sext48(gm2_s24((uint32_t)(gm2_reverb_size_table[idx + 2] + pre_delay)));
  if (pre > 100) {
    pre = 100;
  }
  const int64_t pre_delay_samples = gm2_div48(gm2_mul48(rate_div100, pre), 10);
  int32_t taps[GM2_CHANNELS][GM2_REVERB_EARLY_TAPS] = {{0}, {0}};
  reverb_early_taps(&gm2_reverb_early_l_table[40 * shape], rate_div100, scale, pre_delay_samples,
                    taps[0]);
  reverb_early_taps(&gm2_reverb_early_r_table[40 * shape], rate_div100, scale, pre_delay_samples,
                    taps[1]);
  const int64_t pre_tap =
      gm2_div48(gm2_mul48(rate_div100, gm2_sext48(gm2_reverb_size_table[idx + 4])), 10);

  config->offsets[20] = gm2_s24((uint32_t)(taps[0][0] - REVERB_SETUP_TANK_TOP));
  config->offsets[27] = gm2_s24((uint32_t)(taps[0][2] - taps[0][0]));
  config->offsets[29] = gm2_s24((uint32_t)(taps[0][4] - taps[0][2]));
  config->offsets[31] = gm2_s24((uint32_t)(taps[0][6] - taps[0][4]));
  config->offsets[33] = gm2_s24((uint32_t)(taps[1][0] - taps[0][6]));
  config->offsets[35] = gm2_s24((uint32_t)(taps[1][2] - taps[1][0]));
  config->offsets[37] = gm2_s24((uint32_t)(taps[1][4] - taps[1][2]));
  config->offsets[39] = gm2_s24((uint32_t)(taps[1][6] - taps[1][4]));
  config->offsets[26] = gm2_s24((uint32_t)(taps[0][1] - REVERB_SETUP_TANK_TOP));
  config->offsets[28] = gm2_s24((uint32_t)(taps[0][3] - taps[0][1]));
  config->offsets[30] = gm2_s24((uint32_t)(taps[0][5] - taps[0][3]));
  config->offsets[32] = gm2_s24((uint32_t)(taps[0][7] - taps[0][5]));
  config->offsets[34] = gm2_s24((uint32_t)(taps[1][1] - taps[0][7]));
  config->offsets[36] = gm2_s24((uint32_t)(taps[1][3] - taps[1][1]));
  config->offsets[38] = gm2_s24((uint32_t)(taps[1][5] - taps[1][3]));
  config->offsets[40] = gm2_s24((uint32_t)(taps[1][7] - taps[1][5]));
  config->offsets[41] =
      gm2_s24((uint32_t)(gm2_lo24(REVERB_SETUP_TANK_TOP - pre_tap) - REVERB_SETUP_TANK_TOP));
}

/* P:0x0954A4, the tail of P:0x095730: the two input allpass diffuser gains and
 * their complements. */
static void reverb_diffusers(int decay, int hi_color, int hi_factor, int lo_color, int lo_factor,
                             int diffuse, gm2_reverb_config *config) {
  const int64_t lo = gm2_swf_to_int48(
      reverb_color_multiplier(lo_factor, gm2_reverb_locol_table[2 * lo_color + 1]));
  const int64_t hi = gm2_swf_to_int48(
      reverb_color_multiplier(hi_factor, gm2_reverb_hicol_table[4 * hi_color + 1]));
  int64_t decay_word = gm2_sext48(decay);
  if (lo > 100 || hi > 100) {
    decay_word = gm2_div48(gm2_mul48(decay_word, lo > hi ? lo : hi), 100);
  }
  gm2_swf base;
  if (decay_word < 0x47) {
    base = gm2_swf_div(gm2_swf_mul(gm2_swf_from_int48(decay_word), k_diff_low), k_ten);
  } else {
    base = gm2_swf_add(gm2_swf_div(gm2_swf_mul(gm2_swf_from_int48(decay_word), k_diff_high), k_ten),
                       k_diff_bias);
  }
  gm2_swf amount = gm2_swf_add(gm2_swf_from_int(diffuse), base);
  if (gm2_swf_cmp(amount, gm2_swf_one) < 0) {
    amount = gm2_swf_one;
  } else if (gm2_swf_cmp(amount, k_diff_max) > 0) {
    amount = k_diff_max;
  }
  const int32_t gain = gm2_swf_to_q23(gm2_swf_pow(gm2_swf_div(amount, k_hundred), k_cube_root));
  const int32_t complement = gm2_store24(gm2_macr(gm2_word_acc(0x7FFFFF), -gain, gain));
  config->coef[GM2_REVERB_DIFFUSER] = gain;
  config->coef[GM2_REVERB_DIFFUSER + 1] = complement;
  config->coef[GM2_REVERB_DIFFUSER + 2] = gain;
  config->coef[GM2_REVERB_DIFFUSER + 3] = complement;
}

/* P:0x09526A: the input bandwidth one-pole. */
static void reverb_input_lp(const gm2_rate_words *rate, int hi_color, int hi_factor,
                            gm2_reverb_config *config) {
  const gm2_swf base = gm2_swf_from_int(gm2_reverb_hicol_table[4 * hi_color + 2]);
  const int factor = hi_factor < 0 ? hi_factor * 2 : hi_factor;
  const gm2_swf scale = gm2_swf_mac(gm2_swf_one, k_cent, gm2_swf_from_int(factor));
  const gm2_swf hz = gm2_swf_from_int(gm2_swf_to_int(gm2_swf_mul(scale, base)));
  const int32_t gain = gm2_swf_db_to_lin(gm2_reverb_hicol_table[4 * hi_color + 3]);
  const int32_t cutoff = gm2_swf_to_q23(gm2_swf_exp(gm2_swf_mul(rate->minus_omega, hz)));
  const int32_t one_m_cutoff = gm2_s24((uint32_t)(0x7FFFFF - cutoff));
  const int32_t one_m_gain = gm2_s24((uint32_t)(0x7FFFFF - gain));
  config->coef[GM2_REVERB_INPUT_LP] = cutoff;
  config->coef[GM2_REVERB_INPUT_LP + 1] =
      gm2_store24(gm2_asr_n(gm2_mpyr(one_m_cutoff, one_m_gain), 1u));
  config->coef[GM2_REVERB_INPUT_LP + 2] = gm2_store24(gm2_round24(gm2_mpy_pow2(gain, 1u)));
}

/* P:0x095973: the six early reflection comb feedbacks and the eight tap weights
 * of each channel. */
static void reverb_early_weights(const gm2_rate_words *rate, int shape, int size, int hi_color,
                                 gm2_reverb_config *config) {
  const int group = 9 * hi_color + 72 * shape;
  const gm2_swf size_scale =
      gm2_swf_div(gm2_swf_from_int(gm2_reverb_size_table[5 * size + 45 * shape + 1]), k_hundred);
  const gm2_swf color_scale =
      gm2_swf_div(gm2_swf_from_int(gm2_reverb_color_biquad_table[group]), k_hundred);
  const gm2_swf product = gm2_swf_mul(color_scale, size_scale);
  for (unsigned ch = 0; ch < GM2_CHANNELS; ch++) {
    const int32_t *table =
        (ch == 0u ? gm2_reverb_early_l_table : gm2_reverb_early_r_table) + 40 * shape;
    for (unsigned k = 0; k < GM2_REVERB_EARLY_TAPS; k++) {
      const int32_t *tap = &table[5u * k];
      const gm2_swf hz = gm2_swf_mul(gm2_swf_from_int(tap[2]), product);
      const gm2_swf gain = gm2_swf_exp(gm2_swf_mul(gm2_swf_from_int(tap[1]), k_db_step));
      const gm2_swf lowpass = gm2_swf_exp(gm2_swf_mul(rate->minus_omega, hz));
      gm2_swf weight = gm2_swf_mul(gm2_swf_sub(gm2_swf_one, lowpass), gain);
      if (tap[3] < 0) {
        weight = gm2_swf_neg(weight);
      }
      if (tap[4] == 0 || tap[4] == 3 || tap[4] == 6) {
        config->coef[GM2_REVERB_COMB + GM2_REVERB_EARLY_COMBS * ch + (unsigned)(tap[4] / 3)] =
            gm2_swf_to_q23(lowpass);
      }
      config->early[ch][tap[4]] = gm2_swf_to_q23(weight);
    }
  }
}

/* P:0x095A71: the two early reflection biquads of each channel, from the
 * descriptor pairs at X:0x0991C3 + 9 HiCol + 72 Shape. Both channels run the
 * same pair, and each biquad is staged with the design's second and third words
 * swapped. */
static void reverb_early_biquads(const gm2_rate_words *rate, int shape, int hi_color,
                                 gm2_reverb_config *config) {
  static const unsigned k_order[5] = {0, 2, 1, 3, 4};
  const int32_t *group = &gm2_reverb_color_biquad_table[9 * hi_color + 72 * shape];
  int32_t biquad[GM2_REVERB_EARLY_BIQUADS][5];
  reverb_biquad_design(rate->omega, &group[1], biquad[0]);
  reverb_biquad_design(rate->omega, &group[5], biquad[1]);
  for (unsigned i = 0; i < 5u; i++) {
    config->coef[GM2_REVERB_BIQUAD + i] = biquad[0][k_order[i]];
    config->coef[GM2_REVERB_BIQUAD + 5u + i] = biquad[1][k_order[i]];
    config->coef[GM2_REVERB_BIQUAD + 10u + i] = biquad[0][k_order[i]];
    config->coef[GM2_REVERB_BIQUAD + 15u + i] = biquad[1][k_order[i]];
  }
}

/* P:0x095710: the tank feedback matrix signs. */
static void reverb_tank_signs(gm2_reverb_config *config) {
  for (unsigned i = 0; i < GM2_REVERB_TANK_SIGNS; i++) {
    config->tank_signs[i] = gm2_reverb_tank_signs[i] < 0 ? -0x800000 : 0x7FFFFF;
  }
}

int gm2_reverb_setup_config(const gm2_context *context, const gm2_reverb_setup_raw *raw,
                            gm2_reverb_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }
  const int decay = gm2_clamp_raw(raw->decay, DECAY_MIN, DECAY_MAX);
  const int pre_delay = gm2_clamp_raw(raw->pre_delay, PRE_DELAY_MIN, PRE_DELAY_MAX);
  const int shape = gm2_clamp_raw(raw->shape, SHAPE_MIN, SHAPE_MAX);
  const int size = gm2_clamp_raw(raw->size, SIZE_MIN, EFFECT_SIZE_MAX);
  const int hi_color = gm2_clamp_raw(raw->hi_color, HI_COLOR_MIN, HI_COLOR_MAX);
  const int hi_factor = gm2_clamp_raw(raw->hi_factor, HI_FACTOR_MIN, HI_FACTOR_MAX);
  const int lo_color = gm2_clamp_raw(raw->lo_color, LO_COLOR_MIN, LO_COLOR_MAX);
  const int lo_factor = gm2_clamp_raw(raw->lo_factor, LO_FACTOR_MIN, LO_FACTOR_MAX);
  const int room_level = gm2_clamp_raw(raw->room_level, ROOM_LEVEL_MIN, ROOM_LEVEL_MAX);
  const int reverb_level = gm2_clamp_raw(raw->reverb_level, REVERB_LEVEL_MIN, REVERB_LEVEL_MAX);
  const int diffuse = gm2_clamp_raw(raw->diffuse, DIFFUSE_MIN, DIFFUSE_MAX);

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  reverb_levels(room_level, reverb_level, shape, size, config);
  reverb_damping(&rate_words, decay, hi_color, hi_factor, lo_color, lo_factor, config);
  reverb_tank_offsets(&rate_words, config);
  reverb_early_offsets(&rate_words, shape, size, pre_delay, config);
  reverb_diffusers(decay, hi_color, hi_factor, lo_color, lo_factor, diffuse, config);
  reverb_input_lp(&rate_words, hi_color, hi_factor, config);
  reverb_early_weights(&rate_words, shape, size, hi_color, config);
  reverb_early_biquads(&rate_words, shape, hi_color, config);
  reverb_tank_signs(config);
  gm2_mix_words(raw->out_level, raw->mix, &config->wet, &config->dry);
  return 0;
}
