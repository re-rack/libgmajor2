/* Firmware lookup tables, word for word as the initializer loads them, and the
 * table-driven steps several kernels share. Every effect takes its tables and the
 * log2/exp2 steps from here instead of keeping its own copy.
 *
 * Float tables hold the firmware's (exponent, mantissa) pairs as gm2_swf values.
 * The sine table is the one the firmware computes at run time rather than loads.
 */
#ifndef GM2_TABLES_H
#define GM2_TABLES_H

#include <stdint.h>

#include "gm2_dsp_math.h"
#include "gm2_swf.h"

#define GM2_LOG2_TABLE_WORDS 33u
#define GM2_EXP2_TABLE_WORDS 33u
#define GM2_RATIO_TABLE_ENTRIES 16u
#define GM2_TIME_TABLE_ENTRIES 27u
#define GM2_LFO_SINE_WORDS 256u
#define GM2_NOTE_VALUES 17u
#define GM2_BIQUAD_Q_ENTRIES 33u
#define GM2_REVERB_DECAY_WORDS 513u
#define GM2_REVERB_EARLY_WORDS 120u
#define GM2_REVERB_SIZE_WORDS 135u
#define GM2_REVERB_COLOR_BIQUAD_WORDS 216u
#define GM2_REVERB_LOCOL_WORDS 14u
#define GM2_REVERB_HICOL_WORDS 28u
#define GM2_REVERB_TANK_LENGTH_WORDS 14u
#define GM2_REVERB_TANK_SIGN_WORDS 14u
#define GM2_PHASER_RANGE_WORDS 18u
#define GM2_UNIVIBE_AP_WORDS 8u
#define GM2_MODDELAY_PHAREV_WORDS 15u
#define GM2_MODDELAY_VOICE_GAIN_SETS 5u
#define GM2_MODDELAY_VOICES 6u
#define GM2_MODDELAY_DEPTH_CHO_WORDS 28u
#define GM2_MODDELAY_DEPTH_TZF_WORDS 21u
#define GM2_MODDELAY_WIDTH_WORDS 21u
#define GM2_PITCH_SCALE_COUNT 18u
#define GM2_PITCH_SCALE_STRIDE 8u
#define GM2_PITCH_DET_BIQUAD_WORDS 5u
#define GM2_PITCH_LOG2_POLY_WORDS 6u
#define GM2_PITCH_EXP2_POLY_WORDS 4u

/* X:0x9E8..0xA08: log2(1 + i/32) * 2^17, ten entries one LSB below the rounded
 * formula. */
extern const int32_t gm2_log2_table[GM2_LOG2_TABLE_WORDS];

/* X:0xA09..0xA29: round(2^(i/32) * 2^22), the last entry at the positive rail. */
extern const int32_t gm2_exp2_table[GM2_EXP2_TABLE_WORDS];

/* X:0x1000..0x10FF: 0.5 + 0.5 sin(2 pi i / 256) as P:0x093820 generates it when the
 * Modulation block activates, up to 6 LSB off the rounded formula. The
 * Tremolo/Panner sine waveform reads it, interpolated. */
extern const int32_t gm2_lfo_sine_table[GM2_LFO_SINE_WORDS];

/* Y:0x0100..0x01FF: sin(2 pi i / 256) as P:0x083A02 generates it at boot. The
 * Univibe LFO reads it, interpolated. */
extern const int32_t gm2_sine_table[GM2_LFO_SINE_WORDS];

/* X:0x098ACA..0x098AED: Phaser Range table (High 0..8, Low 9..17), read by
 * P:0x092D6D. */
extern const gm2_swf gm2_phaser_range_table[GM2_PHASER_RANGE_WORDS];

/* X:0x098BB8..0x098BC7: Univibe all-pass frequency table (8 entries), read by
 * P:0x0932C2. */
extern const gm2_swf gm2_univibe_ap_table[GM2_UNIVIBE_AP_WORDS];

/* X:0x1100..0x15FF as P:0x093820 generates them for Tri-Chorus Normal (subtype 4,
 * phase offsets 0x200000 and 0x600000 at X:0x098D25..0x098D2A) and Tri-Chorus
 * Asymmetric (subtype 5, voices 1..5 at phase offsets X:0x098D2C..0x098D30);
 * voices at phase offset 0 read gm2_lfo_sine_table. */
extern const int32_t gm2_trichorus_norm_lfo_table[5][GM2_LFO_SINE_WORDS];
extern const int32_t gm2_trichorus_asym_lfo_table[2][GM2_LFO_SINE_WORDS];
extern const int32_t gm2_trichorus_asym_lfo_set[3][GM2_LFO_SINE_WORDS];

/* Y:0x0030..0x003E: output all-pass / phase-reverse biquad coefficients read by
 * P:0x093CB7 (0..4 for PhaRev = 0, 5..14 for PhaRev = 1). */
extern const int32_t gm2_moddelay_pharev_table[GM2_MODDELAY_PHAREV_WORDS];

/* X:0x098D37..0x098D54: six voice tap gains by subtype group (0 = Chorus/Flanger/
 * Vibrato, 1 = Tri-Chorus Normal, 2 = Tri-Chorus Asymmetric, 3 = TZF Negative,
 * 4 = TZF Positive), read by P:0x093884. */
extern const int32_t gm2_moddelay_voice_gains[GM2_MODDELAY_VOICE_GAIN_SETS][GM2_MODDELAY_VOICES];

/* X:0x098D55..0x098DA1: piecewise-linear Depth tables for Tri-Chorus Normal
 * (X:0x098D55), Tri-Chorus Asymmetric (X:0x098D71) and Through-Zero Flanger
 * (X:0x098D8D), read by P:0x093AC5. */
extern const int32_t gm2_moddelay_depth_norm_table[GM2_MODDELAY_DEPTH_CHO_WORDS];
extern const int32_t gm2_moddelay_depth_asym_table[GM2_MODDELAY_DEPTH_CHO_WORDS];
extern const int32_t gm2_moddelay_depth_tzf_table[GM2_MODDELAY_DEPTH_TZF_WORDS];

/* X:0x098DA2..0x098DCB: piecewise-linear Width tables for Tri-Chorus Normal
 * (X:0x098DA2) and Tri-Chorus Asymmetric (X:0x098DB7), read by P:0x093B1B. */
extern const int32_t gm2_moddelay_width_norm_table[GM2_MODDELAY_WIDTH_WORDS];
extern const int32_t gm2_moddelay_width_asym_table[GM2_MODDELAY_WIDTH_WORDS];

/* X:0x09A254..0x09A2E3: the 18 diatonic and symmetric scale definitions
 * (8 words each: note count, then step intervals in semitones) read by
 * P:0x0910A8 and P:0x091017 for the Intelligent Pitch Shifter. */
extern const int32_t gm2_pitch_scale_table[GM2_PITCH_SCALE_COUNT][GM2_PITCH_SCALE_STRIDE];

/* Y:0x0279..0x027D: the pitch detector input biquad coefficients initialized
 * by P:0x082B85 and read by P:0x000408. */
extern const int32_t gm2_pitch_det_biquad[GM2_PITCH_DET_BIQUAD_WORDS];

/* X:0x0440..0x0445: the 6-word log2 polynomial initialized by P:0x082C5E and
 * read by P:0x000884. */
extern const int32_t gm2_pitch_log2_poly[GM2_PITCH_LOG2_POLY_WORDS];

/* X:0x041F..0x0422: the 4-word exp2 polynomial initialized by P:0x090979 and
 * read by P:0x000E1D. */
extern const int32_t gm2_pitch_exp2_poly[GM2_PITCH_EXP2_POLY_WORDS];

/* X:0x0AD4..0x0AE4 and X:0x0AE5..0x0AF5: Tempo value i lasts gm2_note_beats[i] /
 * gm2_note_divisor[i] beats, from 4 (a whole note) to 1/12 (a 1/32 triplet). Entry
 * 0, Ignore, is never read. */
extern const int32_t gm2_note_beats[GM2_NOTE_VALUES];
extern const int32_t gm2_note_divisor[GM2_NOTE_VALUES];

/* X:0x098DF3..0x098E14: the Delay block's Tempo note factors by Tempo value
 * 1..16, as software-float pairs (4.0 down to 1/12). Entry 0, Ignore, is {0, 0}
 * and never read: the device's two words there are the last Modulation subtype
 * descriptor table pointer and the delay engine swap flag. */
extern const gm2_swf gm2_delay_note_table[GM2_NOTE_VALUES];

/* X:0x098EEB..0x098F0A: the compressor's ratios by raw Ratio, 1.0 (Off) to 100.0
 * (Infinite). */
extern const gm2_swf gm2_ratio_table[GM2_RATIO_TABLE_ENTRIES];

/* X:0x0998D8..0x09990D: times in milliseconds, 0.3 to 7000, by raw index; the
 * compressor's Attack/Release and the delay read it. */
extern const gm2_swf gm2_time_table[GM2_TIME_TABLE_ENTRIES];

/* X:0x000A5C..0x000A9D: biquad Q values by raw index 0..32, read by P:0x08F233. */
extern const gm2_swf gm2_biquad_q_table[GM2_BIQUAD_Q_ENTRIES];

/* Y:0x000952..0x000B52: 513-point exponential decay curve read by P:0x08F161. */
extern const int32_t gm2_reverb_decay_table[GM2_REVERB_DECAY_WORDS];

/* X:0x09904C..0x0990C3 and X:0x0990C4..0x09913B: left and right early reflection
 * tap descriptors (3 shapes * 8 taps * 5 words: delay, gain_db, lp_hz, sign, slot). */
extern const int32_t gm2_reverb_early_l_table[GM2_REVERB_EARLY_WORDS];
extern const int32_t gm2_reverb_early_r_table[GM2_REVERB_EARLY_WORDS];

/* X:0x09913C..0x0991C2: room geometry table by 5 * Size + 45 * Shape. */
extern const int32_t gm2_reverb_size_table[GM2_REVERB_SIZE_WORDS];

/* X:0x0991C3..0x09929A: early reflection HF scale and biquad descriptors by
 * 9 * HiCol + 72 * Shape. */
extern const int32_t gm2_reverb_color_biquad_table[GM2_REVERB_COLOR_BIQUAD_WORDS];

/* X:0x09929B..0x0992A8: Lo Color frequency and decay multiplier pairs (7 * 2). */
extern const int32_t gm2_reverb_locol_table[GM2_REVERB_LOCOL_WORDS];

/* X:0x0992A9..0x0992C4: Hi Color frequency, decay multiplier, input LP frequency
 * and gain quads (7 * 4). */
extern const int32_t gm2_reverb_hicol_table[GM2_REVERB_HICOL_WORDS];

/* X:0x0992C9..0x0992D6: the 7 tank primary and 7 tank secondary delay lengths. */
extern const int32_t gm2_reverb_tank_lengths[GM2_REVERB_TANK_LENGTH_WORDS];

/* X:0x0992DB..0x0992E8: the 14 tank feedback matrix signs (+1 or -1). */
extern const int32_t gm2_reverb_tank_signs[GM2_REVERB_TANK_SIGN_WORDS];

/* P:0x08F161: linear interpolation in gm2_reverb_decay_table at -word, where
 * bits above 5 select the lower entry and bits 5..0 interpolate toward the next. */
int32_t gm2_reverb_decay_interp(int32_t word);

/* The words P:0x090441 and P:0x094CFA install as Y:0x375 / Y:0x569, Y:0x380 /
 * Y:0x576 and Y:0x381 / Y:0x577: the log2 offset and the exp2 rounding pair. */
enum {
  GM2_TABLES_LOG2_OFFSET = 0x020000,
  GM2_TABLES_EXP2_BASE = 0x000A09,
  GM2_TABLES_EXP2_ONE = 0x000001,
};

/* log2 of a 56-bit accumulator as the gate (P:0x000C6A..0x000C76), the
 * compressor (P:0x001284..0x001290) and the wah (P:0x000F3C..0x000F48) compute
 * it: INC, CLB, NORMF, then the two gm2_log2_table entries around the normalized
 * mantissa, interpolated with MAC and MACR, plus the exponent less 0x020000.
 * Returns the 56-bit result. */
static inline int64_t gm2_log2(int64_t acc) {
  /* A valid envelope always normalizes to log-table steps 0x20..0x3F; the mask
   * only keeps an arbitrary state word inside the table. */
  int64_t value = gm2_inc(acc);
  int32_t count = gm2_clb(value);
  value = gm2_normf(value, count);
  value = gm2_asr_n(value, 17);
  int64_t exponent = gm2_asl_n(gm2_word_acc(count), 17);
  unsigned step = (unsigned)gm2_a1(value);
  value = gm2_asr_n(gm2_a0_to_a1(value), 1);
  unsigned index = (step - 0x20u) & 0x1Fu;
  int32_t fraction = gm2_store24(value);
  int32_t low = gm2_log2_table[index];
  int64_t result = gm2_mac(gm2_word_acc(low), fraction, gm2_log2_table[index + 1]);
  result = gm2_macr(result, -low, fraction);
  return gm2_acc56(result + gm2_sub24(exponent, GM2_TABLES_LOG2_OFFSET));
}

/* A 256-word wave table read at an LFO's phase, as every LFO routine reads one
 * (the wah's P:0x0003C7, the Tremolo/Panner's P:0x000EC1, the Univibe's
 * P:0x0010A6 and the Phaser's and the chorus routine's own copies): the entry
 * the phase's top eight bits pick and the next one, wrapping, interpolated by the
 * fifteen bits below. The routines index from the table's base without wrapping,
 * so the phase has to stay in 0..0x7FFFFF, as every setup and align keeps it. */
static inline int32_t gm2_wave_read(const int32_t table[GM2_LFO_SINE_WORDS], int32_t phase) {
  /* The three shifts and moves the routine makes before the interpolation leave the
   * phase's top eight bits as the entry and its fifteen below, times 256, as the
   * fraction of the 24-bit accumulator's interpolating products. */
  const unsigned at = (unsigned)(phase >> 15) & (GM2_LFO_SINE_WORDS - 1u);
  const int32_t fraction = (phase & 0x7FFF) << 8;
  const int32_t low = table[at];
  const int32_t high = table[(at + 1u) & (GM2_LFO_SINE_WORDS - 1u)];
  const int64_t acc = gm2_word_acc(low) + 2 * (int64_t)(high - low) * fraction;
  /* Every table here stays inside the 24-bit rails, and the fraction is a weight
   * below 1, so the result sits between the two entries and cannot rail. */
  return (int32_t)gm2_floor_step24(acc);
}

/* exp2 of a 24-bit word as the gate (P:0x000CA6..0x000CB3) and the compressor
 * (P:0x0012C1..0x0012CE) compute it: bits 16..12 pick a gm2_exp2_table pair, the
 * bits below interpolate it, and NORMF shifts by the bits above. Returns the
 * 56-bit result. */
static inline int64_t gm2_exp2(int32_t word) {
  int64_t index = gm2_add24(gm2_and_a1(gm2_mpy_pow2(word, 12), 0x1Fu), GM2_TABLES_EXP2_BASE);
  unsigned at = (unsigned)gm2_a1(index) - GM2_TABLES_EXP2_BASE;
  int64_t shift = gm2_neg56(gm2_acc56(gm2_mpy_pow2(word, 17) + gm2_word_acc(GM2_TABLES_EXP2_ONE)) &
                            ~0xFFFFFFLL);
  int32_t scale = gm2_s24((uint32_t)gm2_a1(gm2_lsr_a1(gm2_a0_to_a1(index))));
  int32_t high = gm2_exp2_table[at + 1];
  int32_t low = gm2_store24(gm2_word_acc(gm2_exp2_table[at]));
  int64_t table = gm2_mac(gm2_word_acc(gm2_exp2_table[at]), high, scale);
  table = gm2_mac(table, -low, scale);
  return gm2_normf(table, gm2_s24((uint32_t)gm2_a1(shift)));
}

#endif /* GM2_TABLES_H */
