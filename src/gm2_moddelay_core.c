#include "gm2_moddelay_core.h"

#include "gm2_dsp_math.h"
#include "gm2_lfo.h"
#include "gm2_process.h"
#include "gm2_tables.h"

#include <string.h>

enum {
  MODDELAY_VOICES_A_CHANNEL = 3,
  MODDELAY_DEPTH_KNEE = 7,       /* P:0x001176: an LFO word above it keeps the voice's depth */
  MODDELAY_LFO_SCALE = 0x000960, /* Y:0x04BD + 5v: the delay a full-depth LFO word spans */
};

/* P:0x093820 leaves the six voices' tables where the LFO set puts them, one 256-word
 * table per voice at X:0x1000 + 0x100 * voice: the uniform set is the plain sine,
 * the Normal set repeats the 0x200000 and 0x600000 tables, and the Asymmetric set
 * has six distinct offsets. */
static const int32_t *moddelay_lfo_table(gm2_moddelay_core_lfo_set lfo_set, unsigned voice) {
  switch (lfo_set) {
  case GM2_MODDELAY_CORE_LFO_TRICHORUS:
    return voice == 0u ? gm2_lfo_sine_table : gm2_trichorus_norm_lfo_table[voice - 1u];
  case GM2_MODDELAY_CORE_LFO_TRICHORUS_ASYM:
    if (voice == 0u) {
      return gm2_lfo_sine_table;
    }
    if (voice == 1u || voice == 4u) {
      return gm2_trichorus_asym_lfo_table[voice == 1u ? 0u : 1u];
    }
    return gm2_trichorus_asym_lfo_set[voice == 2u ? 0u : voice == 3u ? 1u : 2u];
  case GM2_MODDELAY_CORE_LFO_UNIFORM:
  default:
    return gm2_lfo_sine_table;
  }
}

/* Each doubled product of two 24-bit words is at most 2^47 and no chain below sums
 * more than four of them, so no intermediate passes 2^49 and the firmware's 56-bit
 * accumulator cannot wrap; only its rounding and limiting points have to follow
 * it. */
static inline int64_t moddelay_mul(int32_t s1, int32_t s2) { return 2 * (int64_t)s1 * (int64_t)s2; }

/* The chunk's job run: the thirty words ride one-pole jobs, the Mix pair rides the
 * level record's, and each voice's offset moves by its tap crossfade, behind the
 * settled check P:0x00033C. A word already on its target stays there — the
 * one-pole lands on it exactly — and a crossfade whose both taps already read
 * their target leaves the record alone. */
static void moddelay_jobs(gm2_moddelay_core_state *state, const gm2_moddelay_core_config *config) {
  for (unsigned i = 0; i < GM2_MODDELAY_CORE_COEFS; i++) {
    if (state->coef[i] != config->coef[i]) {
      state->coef[i] = gm2_job_smooth(state->coef[i], config->coef[i], GM2_JOB_SMOOTH_STEP);
    }
  }
  state->wet = gm2_job_smooth(state->wet, config->wet, GM2_JOB_SMOOTH_STEP);
  state->dry = gm2_job_smooth(state->dry, config->dry, GM2_JOB_SMOOTH_STEP);
  for (unsigned v = 0; v < GM2_MODDELAY_CORE_VOICES; v++) {
    if (state->tap_fade[v].step == 0 && state->tap_offset[v][0] == config->tap_offset[v] &&
        state->tap_offset[v][1] == config->tap_offset[v]) {
      continue;
    }
    (void)gm2_job_fade_check(&state->tap_fade[v], state->tap_offset[v], config->tap_offset[v]);
    gm2_job_fade_step(&state->tap_fade[v], state->tap_gain[v]);
  }
}

/* One stereo frame, P:0x001117..0x0011F8, then the chunk's job run once its last
 * frame is done. The inputs arrive as the 24-bit words the task record's input
 * pointer reads. */
void gm2_moddelay_core_process_frame(gm2_moddelay_core_state *state,
                                     const gm2_moddelay_core_config *config,
                                     const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  const int32_t *c = state->coef;

  /* Y:0x04F6's gain reaches the input with the accumulator's sign flipped, and the
   * two Hi-Cut low-passes take it from there. */
  const int32_t in_l = gm2_s24((uint32_t)in[0]);
  const int32_t in_r = gm2_s24((uint32_t)in[1]);
  const int32_t x_in_l = gm2_store24(gm2_round24(moddelay_mul(-GM2_LEVEL_INPUT, in_l)));
  const int32_t x_in_r = gm2_store24(gm2_round24(moddelay_mul(-in_r, GM2_LEVEL_INPUT)));
  const int32_t hi_cut_l =
      gm2_store24(gm2_round24(moddelay_mul(x_in_l, c[GM2_MODDELAY_CORE_HI_CUT + 0]) +
                              moddelay_mul(c[GM2_MODDELAY_CORE_HI_CUT + 1], state->hi_cut[0])));
  const int32_t hi_cut_r =
      gm2_store24(gm2_round24(moddelay_mul(x_in_r, c[GM2_MODDELAY_CORE_HI_CUT + 2]) +
                              moddelay_mul(c[GM2_MODDELAY_CORE_HI_CUT + 3], state->hi_cut[1])));
  state->hi_cut[0] = hi_cut_l;
  state->hi_cut[1] = hi_cut_r;

  /* The previous frame's voice sums through the FB-Cut low-pass and 200 Hz
   * high-pass pair, whose states the two accumulators keep in step. */
  const int32_t feedback = c[GM2_MODDELAY_CORE_FEEDBACK];
  const int32_t u_l = gm2_store24(moddelay_mul(feedback, state->sum[0]));
  const int32_t u_r = gm2_store24(moddelay_mul(feedback, state->sum[1]));
  const int64_t lp_l = moddelay_mul(c[GM2_MODDELAY_CORE_FB_FILTER + 0], state->fb_filter[0][0]) +
                       moddelay_mul(c[GM2_MODDELAY_CORE_FB_FILTER + 2], u_l);
  const int64_t hp_l = moddelay_mul(c[GM2_MODDELAY_CORE_FB_FILTER + 1], state->fb_filter[0][1]) +
                       moddelay_mul(c[GM2_MODDELAY_CORE_FB_FILTER + 3], u_l);
  const int64_t lp_r = moddelay_mul(c[GM2_MODDELAY_CORE_FB_FILTER + 4], state->fb_filter[1][0]) +
                       moddelay_mul(c[GM2_MODDELAY_CORE_FB_FILTER + 6], u_r);
  const int64_t hp_r = moddelay_mul(c[GM2_MODDELAY_CORE_FB_FILTER + 5], state->fb_filter[1][1]) +
                       moddelay_mul(c[GM2_MODDELAY_CORE_FB_FILTER + 7], u_r);
  state->fb_filter[0][0] = gm2_store24(lp_l);
  state->fb_filter[0][1] = gm2_store24(hp_l);
  state->fb_filter[1][0] = gm2_store24(lp_r);
  state->fb_filter[1][1] = gm2_store24(hp_r);

  /* The left buffer takes the left Hi-Cut word and the filtered feedback, the right
   * one the right Hi-Cut word and the right pair's sum, both behind their write
   * pointers and ahead of the decrement. */
  state->buf[0][state->write] =
      gm2_store24(gm2_word_acc(gm2_store24(lp_l + hp_l)) + gm2_word_acc(hi_cut_l));
  state->buf[1][state->write] = gm2_store24(lp_r + hp_r + gm2_word_acc(hi_cut_r));
  state->write = (state->write - 1u) % GM2_MODDELAY_CORE_BUFFER_WORDS;

  /* The phase accumulator wraps when it leaves its 24 bits. */
  state->lfo_phase = gm2_lfo_advance(state->lfo_phase, c[GM2_MODDELAY_CORE_LFO_STEP]);

  /* Six voices, each a pair of taps at the write pointer plus its offset plus a
   * depth-scaled delay. The uniform set puts every voice on the sine table, so one
   * read of the phase serves all six. */
  int32_t wave[GM2_MODDELAY_CORE_VOICES];
  if (config->lfo_set == GM2_MODDELAY_CORE_LFO_UNIFORM) {
    const int32_t one = gm2_wave_read(gm2_lfo_sine_table, state->lfo_phase);
    for (unsigned v = 0; v < GM2_MODDELAY_CORE_VOICES; v++) {
      wave[v] = one;
    }
  } else {
    for (unsigned v = 0; v < GM2_MODDELAY_CORE_VOICES; v++) {
      wave[v] = gm2_wave_read(moddelay_lfo_table(config->lfo_set, v), state->lfo_phase);
    }
  }

  int32_t tap[GM2_MODDELAY_CORE_VOICES][GM2_MODDELAY_CORE_TAPS];
  for (unsigned v = 0; v < GM2_MODDELAY_CORE_VOICES; v++) {
    const int32_t lfo = wave[v];

    /* The frame runs at the depth it started with; at the LFO's trough the voice
     * takes in the config's. */
    const int32_t depth = state->depth[v];
    if (lfo <= MODDELAY_DEPTH_KNEE) {
      state->depth[v] = config->depth[v];
    }
    /* A voice whose gain is zero adds a zero term to its channel sum whatever its
     * taps read, and reading them changes no state. */
    if (c[GM2_MODDELAY_CORE_VOICE_GAIN + v] == 0) {
      tap[v][0] = 0;
      tap[v][1] = 0;
      continue;
    }
    const int32_t travel = gm2_store24(moddelay_mul(lfo, depth));
    const int64_t step = moddelay_mul(travel, MODDELAY_LFO_SCALE);
    const uint32_t whole = (uint32_t)gm2_a1(step);
    const int32_t frac = (int32_t)(((uint32_t)step & 0xFFFFFFu) >> 1);
    const int32_t *buf = state->buf[v / MODDELAY_VOICES_A_CHANNEL];
    for (unsigned t = 0; t < GM2_MODDELAY_CORE_TAPS; t++) {
      if (state->tap_gain[v][t] == 0) {
        tap[v][t] = 0;
        continue;
      }
      const uint32_t at = (state->write + (uint32_t)state->tap_offset[v][t] + whole) %
                          GM2_MODDELAY_CORE_BUFFER_WORDS;
      const int32_t first = buf[at];
      const int32_t second = buf[(at + 1u) % GM2_MODDELAY_CORE_BUFFER_WORDS];
      tap[v][t] =
          gm2_store24(gm2_round24(gm2_word_acc(first) + moddelay_mul(frac, second - first)));
    }
  }

  /* Each voice blends its two taps and the six blend words ride the voice gains
   * Y:0x04E4..Y:0x04E9 onto the two channel sums. */
  int64_t a = moddelay_mul(state->tap_gain[0][0], tap[0][0]) +
              moddelay_mul(tap[0][1], state->tap_gain[0][1]);
  int64_t b = moddelay_mul(state->tap_gain[1][0], tap[1][0]);
  int32_t x0 = gm2_store24(a);
  a = moddelay_mul(x0, c[GM2_MODDELAY_CORE_VOICE_GAIN + 0]);
  b += moddelay_mul(state->tap_gain[1][1], tap[1][1]);
  x0 = gm2_store24(b);
  b = moddelay_mul(tap[2][0], state->tap_gain[2][0]);
  a += moddelay_mul(c[GM2_MODDELAY_CORE_VOICE_GAIN + 1], x0);
  b += moddelay_mul(tap[2][1], state->tap_gain[2][1]);
  const int32_t x1 = gm2_store24(b);
  a = gm2_round24(a + moddelay_mul(x1, c[GM2_MODDELAY_CORE_VOICE_GAIN + 2]));
  const int32_t sum_l = gm2_store24(a);

  b = moddelay_mul(tap[3][0], state->tap_gain[3][0]) +
      moddelay_mul(tap[3][1], state->tap_gain[3][1]);
  x0 = gm2_store24(b);
  a = moddelay_mul(state->tap_gain[4][0], tap[4][0]);
  b = moddelay_mul(x0, c[GM2_MODDELAY_CORE_VOICE_GAIN + 3]);
  a += moddelay_mul(state->tap_gain[4][1], tap[4][1]);
  x0 = gm2_store24(a);
  a = moddelay_mul(tap[5][0], state->tap_gain[5][0]);
  b += moddelay_mul(c[GM2_MODDELAY_CORE_VOICE_GAIN + 4], x0);
  a += moddelay_mul(tap[5][1], state->tap_gain[5][1]);
  x0 = gm2_store24(a);
  b = gm2_round24(b + moddelay_mul(c[GM2_MODDELAY_CORE_VOICE_GAIN + 5], x0));
  const int32_t sum_r = gm2_store24(b);

  state->sum[0] = sum_l;
  state->sum[1] = sum_r;

  /* The output biquad, one per channel at X:0x006C..X:0x0073, then Y:0x04F7's
   * pair: the wet word on the biquad's output and the dry word on the frame's
   * input, the two products rounded together once. */
  const int32_t wet = state->wet;
  const int32_t dry = state->dry;
  const int32_t dry_in[GM2_CHANNELS] = {in_l, in_r};
  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    int32_t *last = state->out_in[channel];
    int32_t x[3] = {last[0], last[1], state->sum[channel]};
    last[1] = last[0];
    last[0] = x[2];
    (void)gm2_biquad_step(&state->out_biquad[channel],
                          &c[GM2_MODDELAY_CORE_OUT_BIQUAD + GM2_BIQUAD_COEFS * channel], x);
    out[channel] =
        gm2_store24(gm2_round24(moddelay_mul(wet, x[2]) + moddelay_mul(dry_in[channel], dry)));
  }

  if (++state->chunk_pos < GM2_CHUNK_FRAMES) {
    return;
  }
  state->chunk_pos = 0;
  moddelay_jobs(state, config);
}

void gm2_moddelay_core_state_init(gm2_moddelay_core_state *state,
                                  const gm2_moddelay_core_config *config) {
  memset(state, 0, sizeof *state);
  for (unsigned i = 0; i < GM2_MODDELAY_CORE_COEFS; i++) {
    state->coef[i] = config->coef[i];
  }
  for (unsigned v = 0; v < GM2_MODDELAY_CORE_VOICES; v++) {
    state->depth[v] = config->depth[v];
    state->tap_offset[v][0] = config->tap_offset[v];
    state->tap_offset[v][1] = config->tap_offset[v];
    gm2_job_fade_init(&state->tap_fade[v], state->tap_gain[v]);
  }
  state->wet = config->wet;
  state->dry = config->dry;
}

void gm2_moddelay_core_align(gm2_moddelay_core_state *state, const gm2_moddelay_core_config *config,
                             double beats) {
  if (config->note != 0u) {
    state->lfo_phase = gm2_lfo_phase_at(beats, config->note);
  }
}
