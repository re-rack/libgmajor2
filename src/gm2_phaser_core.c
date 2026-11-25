/* G-Major 2 Phaser audio core: the routine P:0x000FA8..0x001060 that the Vintage
 * and Smooth Phasers share.
 *
 * One frame, in the order the routine runs it:
 *
 *   mono    = (mpyr(1.0, in_L) + mpyr(in_R, 1.0)) >> 1
 *   phase   = (phase + step) wrapped to zero when it leaves 24 bits
 *   lfo     = the unipolar sine table, interpolated at the phase's fraction
 *   shaped  = (1.0 - depth)/2 + lfo * depth
 *   sweep[k]= 2 (0.5 - (base[k] + slope[k] * shaped)^3)
 *   g_low   = sweep[0] - 0.01,  g_high = sweep[2] - 0.01   (left branch)
 *   g_low   = sweep[2],         g_high = sweep[0]          (right branch)
 *   damped  = the two-pole damping filter on the feedback sum
 *   clipped = min(0, |damped| - 0.25), then 2 (1.0 - clipped^2), signed
 *   filtered= 4 (low-pass[0] clipped + low-pass[1] state) + 4 low-pass[2] clipped
 *   pair    = the all-pass section at sweep[1] on mono
 *   input   = pair + feedback * filtered
 *   left    = four all-pass sections on input, g_low g_high pole[0] pole[1]
 *   right   = three all-pass sections on input, g_low g_high pole[2]
 *   feedback= mono + the left chain's first and last taps
 *   wet_L   = the left taps;  wet_R = the right gain times the right taps
 *   out     = wet * wet gain + dry * dry gain, rounded
 *
 * The 0.01 word 0x0147AE at Y:0x043B and Y:0x043C is what the left branch's
 * section words subtract and the right branch's do not, which is the stereo phase
 * offset; the tap weights pick the four-stage (Vintage) or twelve-stage (Smooth)
 * all-pass output.
 *
 * Every chain below is one int64 sum. A 24-bit word is at most 2^23 and a doubled
 * product of two is at most 2^47, so the widest sum here, three of them, is below
 * 2^49: no intermediate reaches the 2^55 the firmware's accumulator wraps at, and
 * only the rounding and limiting points have to follow it.
 */
#include "gm2_phaser_core.h"

#include "gm2_dsp_math.h"
#include "gm2_job.h"
#include "gm2_lfo.h"
#include "gm2_process.h"
#include "gm2_tables.h"

/* The words the routine reads from Y rather than from the coefficient set. */
enum {
  PHASER_SWEEP_MID = 0x0147AE, /* Y:0x043B and Y:0x043C, 0.01 */
  PHASER_DAMP_0 = 0x7D9168,    /* Y:0x043D */
  PHASER_DAMP_1 = 0x7B020C,    /* Y:0x043E */
  PHASER_DAMP_2 = -0x68D4FE,   /* Y:0x043F, 0x972B02 as a signed word */
  PHASER_DAMP_3 = 0x74FDF4,    /* Y:0x0440 */
  PHASER_DAMP_4 = 0x5DD2F2,    /* Y:0x0441 */
  PHASER_TAP_GAIN = 0x7FFFFF,  /* Y:0x0457 */
  PHASER_CLIP_KNEE = 0x200000, /* the routine's knee immediate, 0.25 */
  PHASER_CLIP_BASE = 0x080000, /* 1.0, the parabola's base word */
};

/* One of the three Range curves: the base and slope the k'th pair of Y:0x0435..0x043A
 * gives, driven by the LFO's shaped word, then 2 (0.5 - driven^3). */
static inline int32_t phaser_sweep(const int32_t *c, int32_t shaped, unsigned k) {
  const int32_t word = gm2_store24((int64_t)c[GM2_PHASER_CORE_SWEEP + 2u * k + 1u] * 0x1000000 +
                                   2 * (int64_t)shaped * c[GM2_PHASER_CORE_SWEEP + 2u * k]);
  const int32_t square = gm2_store24(2 * (int64_t)word * word);
  return gm2_store24(0x800000000000LL - 4 * (int64_t)word * square);
}

/* One all-pass section, the routine's repeated parcel:
 * the section's own 24-bit input `in`, the word `a_old` its boundary slot holds
 * before this frame, that slot's other pair, at coefficient `g`.
 *
 *   p = g in + g b - a_old,  q = g c - b + g p,  *b := p, *c := q
 *
 * `b_slot` and `c_old` are the section's own `*b` and its `*c` before this frame.
 * Both sums are exact in int64: p is below 2^49 and q below 2^48. Returns q as the
 * accumulator the caller continues from.
 *
 * The boundary write is the previous section's own `*c` write of the same word,
 * so only the chain's first section needs it. */
static inline int64_t phaser_section(int32_t g, int32_t in, int32_t a_old, int32_t c_old,
                                     int32_t *b_slot) {
  const int32_t b_old = *b_slot;
  const int32_t p = gm2_store24(2 * (int64_t)g * (in + b_old) - (int64_t)a_old * 0x1000000);
  *b_slot = p;
  return 2 * (int64_t)g * (c_old + p) - (int64_t)b_old * 0x1000000;
}

/* The chunk's job run: the step and the depth ramp, the other words ride a
 * one-pole job. */
static void phaser_jobs(gm2_phaser_core_state *state, const gm2_phaser_core_config *config) {
  const int armed[GM2_PHASER_CORE_RAMPS] = {
      [GM2_PHASER_CORE_STEP] =
          config->raw[GM2_PHASER_CORE_RAW_SPEED] != state->raw[GM2_PHASER_CORE_RAW_SPEED] ||
          config->raw[GM2_PHASER_CORE_RAW_TEMPO] != state->raw[GM2_PHASER_CORE_RAW_TEMPO],
      [GM2_PHASER_CORE_DEPTH] =
          config->raw[GM2_PHASER_CORE_RAW_DEPTH] != state->raw[GM2_PHASER_CORE_RAW_DEPTH],
  };
  for (unsigned i = 0; i < GM2_PHASER_CORE_RAWS; i++) {
    state->raw[i] = config->raw[i];
  }
  for (unsigned i = 0; i < GM2_PHASER_CORE_RAMPS; i++) {
    if (armed[i]) {
      state->ramp_step[i] = gm2_job_ramp_arm(state->coef[i], config->coef[i]);
    }
    state->coef[i] = gm2_job_ramp(state->coef[i], config->coef[i], state->ramp_step[i]);
  }
  for (unsigned i = GM2_PHASER_CORE_RAMPS; i < GM2_PHASER_CORE_COEFS; i++) {
    state->coef[i] = gm2_job_smooth(state->coef[i], config->coef[i], GM2_JOB_SMOOTH_STEP);
  }
}

/* One stereo frame, P:0x000FA8..0x001060, then the chunk's job run once its last
 * frame is done. The chain's words are kept in locals between sections: the
 * firmware reads each section's `*c` back out of memory, and the same value
 * carried forward keeps the frame's dependency chain off the store buffer. */
void gm2_phaser_core_process_frame(gm2_phaser_core_state *state,
                                   const gm2_phaser_core_config *config,
                                   const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  const int32_t *c = state->coef;

  const int64_t sum = gm2_mpyr(GM2_LEVEL_INPUT, gm2_s24((uint32_t)in[0])) +
                      gm2_mpyr(gm2_s24((uint32_t)in[1]), GM2_LEVEL_INPUT);
  const int32_t mono = gm2_store24(gm2_asr_n(sum, 1));

  /* The phase wraps to zero, and the sine table is read at it. */
  state->lfo_phase = gm2_lfo_advance_restart(state->lfo_phase, c[GM2_PHASER_CORE_STEP]);
  const int32_t lfo = gm2_wave_read(gm2_lfo_sine_table, state->lfo_phase);

  /* Depth scales the table's [0, 1] onto [(1 - depth)/2, (1 + depth)/2]. */
  const int32_t shaped =
      gm2_store24(gm2_asr_n((int64_t)(GM2_W24_MAX - c[GM2_PHASER_CORE_DEPTH]) * 0x1000000, 1) +
                  2 * (int64_t)lfo * c[GM2_PHASER_CORE_DEPTH]);

  /* The three range curves: a base and a slope a pair of the Range table's words
   * gives, then 2 (0.5 - driven^3). */
  const int32_t sweep_0 = phaser_sweep(c, shaped, 0u);
  const int32_t sweep_1 = phaser_sweep(c, shaped, 1u);
  const int32_t sweep_2 = phaser_sweep(c, shaped, 2u);

  /* The damping filter on the previous frame's feedback sum, two poles on the
   * fixed words Y:0x043D..Y:0x0441. */
  const int32_t feedback_in = state->feedback;
  const int32_t prev_out = state->damp[0];
  const int32_t prev_damped = state->damp[1];
  state->damp[0] = feedback_in;
  const int32_t damped = gm2_store24(2 * (int64_t)PHASER_DAMP_0 * (feedback_in - prev_out) +
                                     2 * (int64_t)PHASER_DAMP_1 * prev_damped);
  const int64_t poles =
      2 * ((int64_t)PHASER_DAMP_2 * prev_damped + (int64_t)PHASER_DAMP_3 * damped) +
      2 * (int64_t)state->damp[2] * PHASER_DAMP_4;
  state->damp[1] = damped;
  state->damp[2] = gm2_store24(poles);

  /* The clip P:0x0003AF inlined: past the knee the excess is held at zero, and the
   * parabola bends whatever is left. */
  int64_t clip = (poles < 0 ? -poles : poles) - (int64_t)PHASER_CLIP_KNEE * 0x1000000;
  if (clip > 0) {
    clip = 0;
  }
  const int32_t excess = gm2_store24(clip);
  clip = 2 * ((int64_t)PHASER_CLIP_BASE * 0x1000000 - 2 * (int64_t)excess * excess);
  if (poles < 0) {
    clip = -clip;
  }
  const int32_t feedback = gm2_store24(clip);

  /* The post-clip low-pass Y:0x0442..Y:0x0444, its two halves scaled by four. */
  const int64_t low = 2 * ((int64_t)feedback * c[GM2_PHASER_CORE_LOW_PASS] +
                           (int64_t)c[GM2_PHASER_CORE_LOW_PASS + 1u] * state->low_pass);
  const int32_t filtered =
      gm2_store24(4 * low + 8 * (int64_t)feedback * c[GM2_PHASER_CORE_LOW_PASS + 2u]);
  state->low_pass = gm2_store24(4 * low);

  /* The input pair, the same section at the LFO's own curve word. */
  const int64_t pair =
      phaser_section(sweep_1, mono, state->pair[0], state->pair[2], &state->pair[1]);
  state->pair[0] = mono;
  state->pair[2] = gm2_store24(pair);
  const int32_t chain_in = gm2_store24(pair + 2 * (int64_t)c[GM2_PHASER_CORE_FEEDBACK] * filtered);

  /* Each section's boundary word is the previous section's `*c` as the last frame
   * left it, which this frame's write of that word replaces. The two chains are
   * independent from `chain_in` on, so they alternate below to keep two multiply
   * chains in flight. */
  const int32_t left_a0 = state->left[0];
  const int32_t left_c0 = state->left[2];
  const int32_t left_c1 = state->left[4];
  const int32_t left_c2 = state->left[6];
  const int32_t left_c3 = state->left[8];
  const int32_t right_a0 = state->right[0];
  const int32_t right_c0 = state->right[2];
  const int32_t right_c1 = state->right[4];
  const int32_t right_c2 = state->right[6];

  const int32_t left_g_low = gm2_store24(gm2_word_acc(sweep_0 - PHASER_SWEEP_MID));
  const int32_t left_g_high = gm2_store24(gm2_word_acc(sweep_2 - PHASER_SWEEP_MID));
  state->left[0] = chain_in;
  state->right[0] = chain_in;
  const int32_t l1 =
      gm2_store24(phaser_section(left_g_low, chain_in, left_a0, left_c0, &state->left[1]));
  const int32_t r1 =
      gm2_store24(phaser_section(sweep_2, chain_in, right_a0, right_c0, &state->right[1]));
  const int32_t l2 =
      gm2_store24(phaser_section(left_g_high, l1, left_c0, left_c1, &state->left[3]));
  const int32_t r2 = gm2_store24(phaser_section(sweep_0, r1, right_c0, right_c1, &state->right[3]));
  const int32_t l3 =
      gm2_store24(phaser_section(c[GM2_PHASER_CORE_POLE], l2, left_c1, left_c2, &state->left[5]));
  const int32_t r3 = gm2_store24(
      phaser_section(c[GM2_PHASER_CORE_POLE + 2u], r2, right_c1, right_c2, &state->right[5]));
  const int32_t l4 = gm2_store24(
      phaser_section(c[GM2_PHASER_CORE_POLE + 1u], l3, left_c2, left_c3, &state->left[7]));
  state->left[2] = l1;
  state->left[4] = l2;
  state->left[6] = l3;
  state->left[8] = l4;
  state->right[2] = r1;
  state->right[4] = r2;
  state->right[6] = r3;

  /* The tap mix: the staged tap weights pick the four-stage or twelve-stage
   * output, and the mono sum is what the next frame's damping filter takes. */
  const int64_t left = 2 * ((int64_t)c[GM2_PHASER_CORE_TAP_LEFT] * l1 +
                            (int64_t)c[GM2_PHASER_CORE_TAP_LEFT + 1u] * l4);
  const int32_t wet_r =
      gm2_store24(2 *
                  (int64_t)gm2_store24(2 * ((int64_t)c[GM2_PHASER_CORE_TAP_RIGHT] * r1 +
                                            (int64_t)c[GM2_PHASER_CORE_TAP_RIGHT + 1u] * r3)) *
                  c[GM2_PHASER_CORE_RIGHT_GAIN]);
  state->feedback = gm2_store24((int64_t)mono * 0x1000000 + left);
  const int32_t wet_l = gm2_store24(2 * (int64_t)gm2_store24(left) * PHASER_TAP_GAIN);

  /* The wet word; the zero dry term's MACR still rounds. */
  out[0] = gm2_store24(gm2_mpyr(GM2_LEVEL_WET, wet_l));
  out[1] = gm2_store24(gm2_mpyr(wet_r, GM2_LEVEL_WET));

  if (++state->chunk_pos < GM2_CHUNK_FRAMES) {
    return;
  }
  state->chunk_pos = 0;
  phaser_jobs(state, config);
}

void gm2_phaser_core_state_init(gm2_phaser_core_state *state,
                                const gm2_phaser_core_config *config) {
  for (unsigned i = 0; i < 3u; i++) {
    state->damp[i] = 0;
  }
  state->low_pass = 0;
  for (unsigned i = 0; i < 3u; i++) {
    state->pair[i] = 0;
  }
  for (unsigned i = 0; i < 9u; i++) {
    state->left[i] = 0;
  }
  for (unsigned i = 0; i < 7u; i++) {
    state->right[i] = 0;
  }
  state->feedback = 0;
  state->lfo_phase = 0;
  for (unsigned i = 0; i < GM2_PHASER_CORE_COEFS; i++) {
    state->coef[i] = config->coef[i];
  }
  for (unsigned i = 0; i < GM2_PHASER_CORE_RAMPS; i++) {
    state->ramp_step[i] = 1;
  }
  for (unsigned i = 0; i < GM2_PHASER_CORE_RAWS; i++) {
    state->raw[i] = config->raw[i];
  }
  state->chunk_pos = 0;
}

void gm2_phaser_core_align(gm2_phaser_core_state *state, const gm2_phaser_core_config *config,
                           double beats) {
  if (config->note != 0u) {
    state->lfo_phase = gm2_lfo_phase_at(beats, config->note);
  }
}
