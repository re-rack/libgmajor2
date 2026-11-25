#include "gm2_lfo_gain_core.h"

#include "gm2_dsp_math.h"
#include "gm2_job.h"
#include "gm2_lfo.h"
#include "gm2_process.h"
#include "gm2_tables.h"

/* Each doubled product of two 24-bit words is at most 2^47 and no chain below adds
 * more than two of them to a word at the A1 position, so no intermediate passes
 * 2^48 and the firmware's 56-bit accumulator cannot wrap; only its rounding and
 * limiting points have to follow it. The Soft branch is the exception: its ASL #10
 * does wrap and gm2_asl_n keeps that. */
static inline int64_t lfo_gain_mul(int32_t s1, int32_t s2) { return 2 * (int64_t)s1 * (int64_t)s2; }

/* The ramp both Soft branches share: the phase or its distance to the wrap word,
 * times the slope, scaled up ten bits, then wrapped the same way. */
static int64_t lfo_gain_ramp(int32_t phase, int32_t slope) {
  int64_t b = gm2_asl_n(gm2_mpy(phase, slope), 10);
  if (gm2_es(b)) {
    b = gm2_sub24(b, GM2_W24_MAX);
  }
  return b;
}

/* 0x0E9A..0x0EA9: the triangle, whose peak sits on the pulse word. */
static int32_t lfo_gain_soft(const int32_t *coef, int32_t phase) {
  if (phase >= coef[GM2_LFO_GAIN_CORE_PULSE]) {
    int64_t below = gm2_sub24(gm2_word_acc(phase), GM2_W24_MAX);
    return gm2_store24(lfo_gain_ramp(gm2_store24(below), coef[GM2_LFO_GAIN_CORE_FALL]));
  }
  return gm2_store24(lfo_gain_ramp(phase, coef[GM2_LFO_GAIN_CORE_RISE]));
}

/* One channel of the waveform routine: the level the phase asks for, through the
 * routine's one-pole. */
static int32_t lfo_gain_level(gm2_lfo_gain_core_state *state,
                              const gm2_lfo_gain_core_config *config, int32_t phase,
                              unsigned channel) {
  int32_t raw;
  switch (config->wave) {
  case GM2_LFO_GAIN_CORE_WAVE_SOFT:
    raw = lfo_gain_soft(state->coef, phase);
    break;
  case GM2_LFO_GAIN_CORE_WAVE_HARD:
    /* 0x0EAF..0x0EB1: a square wave, high below the pulse word. */
    raw = phase < state->coef[GM2_LFO_GAIN_CORE_PULSE] ? GM2_W24_MAX : 0;
    break;
  case GM2_LFO_GAIN_CORE_WAVE_SINE:
  default:
    /* 0x0EC1..0x0ECE */
    raw = gm2_wave_read(gm2_lfo_sine_table, phase);
    break;
  }
  int64_t b = gm2_round24(lfo_gain_mul(config->smooth[0], state->level[channel]) +
                          lfo_gain_mul(raw, config->smooth[1]));
  state->level[channel] = gm2_store24(b);
  return state->level[channel];
}

/* The chunk's job run: the first five words ramp, each Hi-Cut pair rides a
 * one-pole job. A word already on its target stays there — both jobs land on it
 * exactly — so only a word that differs is stepped. */
static void lfo_gain_jobs(gm2_lfo_gain_core_state *state, const gm2_lfo_gain_core_config *config) {
  const int rate =
      config->raw[GM2_LFO_GAIN_CORE_RAW_SPEED] != state->raw[GM2_LFO_GAIN_CORE_RAW_SPEED] ||
      config->raw[GM2_LFO_GAIN_CORE_RAW_TEMPO] != state->raw[GM2_LFO_GAIN_CORE_RAW_TEMPO] ||
      config->raw[GM2_LFO_GAIN_CORE_RAW_PULSE_WIDTH] !=
          state->raw[GM2_LFO_GAIN_CORE_RAW_PULSE_WIDTH];
  const int depth =
      config->raw[GM2_LFO_GAIN_CORE_RAW_DEPTH] != state->raw[GM2_LFO_GAIN_CORE_RAW_DEPTH];
  for (unsigned i = 0; i < GM2_LFO_GAIN_CORE_RAWS; i++) {
    state->raw[i] = config->raw[i];
  }
  for (unsigned i = 0; i < GM2_LFO_GAIN_CORE_RAMPS; i++) {
    if (i == GM2_LFO_GAIN_CORE_DEPTH ? depth : rate) {
      state->ramp_step[i] = gm2_job_ramp_arm(state->coef[i], config->coef[i]);
    }
    if (state->coef[i] != config->coef[i]) {
      state->coef[i] = gm2_job_ramp(state->coef[i], config->coef[i], state->ramp_step[i]);
    }
  }
  for (unsigned i = GM2_LFO_GAIN_CORE_RAMPS; i < GM2_LFO_GAIN_CORE_COEFS; i++) {
    if (state->coef[i] != config->coef[i]) {
      state->coef[i] = gm2_job_smooth(state->coef[i], config->coef[i], GM2_JOB_SMOOTH_STEP);
    }
  }
}

/* One stereo frame, P:0x000E5A..0x000E8B, then the chunk's job run once its last
 * frame is done. */
void gm2_lfo_gain_core_process_frame(gm2_lfo_gain_core_state *state,
                                     const gm2_lfo_gain_core_config *config,
                                     const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  const int32_t *coef = state->coef;

  /* 0x0E60..0x0E64: the phase advances by the step. */
  state->phase = gm2_lfo_advance(state->phase, coef[GM2_LFO_GAIN_CORE_STEP]);

  /* 0x0E65..0x0E69: the right channel reads the phase a lag behind, adding the
   * wrap word when the difference went negative. */
  int64_t behind = gm2_word_acc(state->phase) - gm2_word_acc(config->lag);
  if (behind < 0) {
    behind += gm2_word_acc(GM2_W24_MAX);
  }

  /* 0x0E6A..0x0E6E: each channel's level through the waveform routine, then the
   * gain, one less than the depth times that level. */
  const int32_t channel_phase[GM2_CHANNELS] = {state->phase, gm2_store24(behind)};
  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    const int32_t *hi_cut = &coef[GM2_LFO_GAIN_CORE_HI_CUT + 2u * channel];
    int32_t level = lfo_gain_level(state, config, channel_phase[channel], channel);
    int32_t gain = gm2_store24(gm2_word_acc(GM2_W24_MAX) +
                               lfo_gain_mul(-level, coef[GM2_LFO_GAIN_CORE_DEPTH]));

    /* 0x0E73..0x0E7C: the input times the gain, rounded, through the channel's
     * Hi-Cut pole. */
    int32_t wet = gm2_store24(gm2_round24(lfo_gain_mul(gain, gm2_s24((uint32_t)in[channel]))));
    state->hi_cut[channel] = gm2_store24(gm2_round24(
        lfo_gain_mul(hi_cut[0], state->hi_cut[channel]) + lfo_gain_mul(wet, hi_cut[1])));

    /* 0x0E83..0x0E89: the wet word times the Hi-Cut state; the zero dry term's
     * MACR still rounds. */
    out[channel] = gm2_store24(gm2_round24(lfo_gain_mul(GM2_LEVEL_WET, state->hi_cut[channel])));
  }

  if (++state->chunk_pos < GM2_CHUNK_FRAMES) {
    return;
  }
  state->chunk_pos = 0;
  lfo_gain_jobs(state, config);
}

void gm2_lfo_gain_core_state_init(gm2_lfo_gain_core_state *state,
                                  const gm2_lfo_gain_core_config *config) {
  state->phase = 0;
  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    state->level[channel] = 0;
    state->hi_cut[channel] = 0;
  }
  for (unsigned i = 0; i < GM2_LFO_GAIN_CORE_COEFS; i++) {
    state->coef[i] = config->coef[i];
  }
  for (unsigned i = 0; i < GM2_LFO_GAIN_CORE_RAMPS; i++) {
    state->ramp_step[i] = 1;
  }
  for (unsigned i = 0; i < GM2_LFO_GAIN_CORE_RAWS; i++) {
    state->raw[i] = config->raw[i];
  }
  state->chunk_pos = 0;
}

void gm2_lfo_gain_core_align(gm2_lfo_gain_core_state *state, const gm2_lfo_gain_core_config *config,
                             double beats) {
  if (config->note != 0u) {
    state->phase = gm2_lfo_phase_at(beats, config->note);
  }
}
