#include "gm2_univibe_core.h"

#include "gm2_dsp_math.h"
#include "gm2_job.h"
#include "gm2_lfo.h"
#include "gm2_process.h"
#include "gm2_tables.h"

enum {
  UNIVIBE_LFO_OFFSET = 0x400000,     /* Y:0x0472: the one half the depth scales the sine around */
  UNIVIBE_BRIGHTNESS_MAX = 0x0FF000, /* the waveshaper's ceiling */
  UNIVIBE_MIX = 0x5A827B, /* Y:0x0488..Y:0x048A: the output mix's other three weights */
};

/* Y:0x048B..Y:0x0490 and Y:0x0491..Y:0x0496, the words of the two soft clips
 * P:0x001061: post shift, rail, knee, end, scale and curve shift. */
static const int32_t k_input_clip[6] = {2, 0x200000, 0x16A77E, 0x295882, 0x6D9196, 2};
static const int32_t k_output_clip[6] = {2, 0x200000, 0x1C8521, 0x237ADF, 0x499058, 4};

/* P:0x001061: the 6-word soft clipper {post shift, rail, knee, end, scale, curve
 * shift} the routine runs on the mono input and on both wet words. Its squared
 * term is at most 2^48 before the curve shift and below 2^52 after it, and the
 * post shift is two, so the whole clipper stays inside int64 and never reaches
 * the firmware's 2^55 wrap. */
static inline int64_t univibe_clip(int64_t acc, const int32_t words[6]) {
  int64_t a = acc < 0 ? -acc : acc;
  if (a > (int64_t)words[2] * 0x1000000) {
    a -= (int64_t)words[3] * 0x1000000;
    if (a >= 0) {
      a = 0;
    }
    const int32_t over = gm2_store24(a);
    const int32_t square = gm2_store24(-2 * (int64_t)over * over);
    a = 2 * (int64_t)square * words[4];
    a = a * ((int64_t)1 << ((unsigned)words[5] & 0x3Fu)) + (int64_t)words[1] * 0x1000000;
  }
  if (acc < 0) {
    a = -a;
  }
  return a * ((int64_t)1 << ((unsigned)words[0] & 0x3Fu));
}

/* One all-pass stage P:0x0010E3..0x0010F3: the stage's own `*x` word, its
 * neighbour's and the coefficient, summed with the incoming word. */
static inline int32_t univibe_stage(int32_t *slot, int32_t next, int32_t pole, int32_t in) {
  const int32_t out =
      gm2_store24((int64_t)*slot * 0x1000000 + 2 * ((int64_t)next * pole - (int64_t)pole * in));
  *slot = in;
  return out;
}

/* The chunk's job run: the depth ramps, the other words ride one-pole jobs. */
static void univibe_jobs(gm2_univibe_core_state *state, const gm2_univibe_core_config *config) {
  for (unsigned i = 0; i < GM2_UNIVIBE_CORE_RAMPS; i++) {
    if (config->raw[i] != state->raw[i]) {
      state->raw[i] = config->raw[i];
      state->ramp_step[i] = gm2_job_ramp_arm(state->coef[i], config->coef[i]);
    }
    state->coef[i] = gm2_job_ramp(state->coef[i], config->coef[i], state->ramp_step[i]);
  }
  for (unsigned i = GM2_UNIVIBE_CORE_RAMPS; i < GM2_UNIVIBE_CORE_COEFS; i++) {
    state->coef[i] = gm2_job_smooth(state->coef[i], config->coef[i], GM2_JOB_SMOOTH_STEP);
  }
}

/* One stereo frame, P:0x00107F..0x001116, then the chunk's job run once its last
 * frame is done. */
void gm2_univibe_core_process_frame(gm2_univibe_core_state *state,
                                    const gm2_univibe_core_config *config,
                                    const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  const int32_t *coef = state->coef;

  /* P:0x001085..0x001091: the two input words, the mono sum, and the input soft
   * clipper. */
  const int64_t left_in = gm2_mpyr(GM2_LEVEL_INPUT, gm2_s24((uint32_t)in[0]));
  const int64_t right_in = gm2_mpyr(gm2_s24((uint32_t)in[1]), GM2_LEVEL_INPUT);
  const int32_t gained_l = gm2_store24(left_in);
  const int32_t gained_r = gm2_store24(right_in);
  int32_t wet = gm2_store24(univibe_clip(gm2_asr_n(left_in + right_in, 3), k_input_clip));

  /* P:0x001094..0x0010D5: the first frame of each four-frame chunk. */
  if (state->chunk_pos == 0u) {
    /* P:0x001096..0x001098: the LFO speed slewer P:0x001072, on the release poles
     * while the target is not above the live word. */
    const int32_t target = config->lfo_speed;
    int64_t a;
    if (gm2_word_acc(target) <= gm2_word_acc(state->lfo_speed)) {
      a = 2 * ((int64_t)target * coef[GM2_UNIVIBE_CORE_SPEED_POLES + 0] +
               (int64_t)state->lfo_speed * coef[GM2_UNIVIBE_CORE_SPEED_POLES + 1]);
    } else {
      a = 2 * ((int64_t)target * coef[GM2_UNIVIBE_CORE_SPEED_POLES + 2] +
               (int64_t)state->lfo_speed * coef[GM2_UNIVIBE_CORE_SPEED_POLES + 3]);
    }
    state->lfo_speed = gm2_store24(a);
    const int32_t step = gm2_store24(gm2_asr_n(a, 8));

    /* P:0x00109B..0x0010AD: the phase a step on, and the sine table at it. */
    state->lfo_phase = gm2_lfo_advance_restart(state->lfo_phase, step);
    const int32_t sine = gm2_wave_read(gm2_sine_table, state->lfo_phase);

    /* P:0x0010AE..0x0010BF: the depth scale, the waveshaper's linear and square
     * weights Y:0x0474/0x0475, and the ceiling. */
    const int32_t shaped =
        gm2_store24(0x400000000000LL + 2 * (int64_t)sine * coef[GM2_UNIVIBE_CORE_DEPTH]);
    const int32_t square = gm2_store24(2 * (int64_t)shaped * shaped);
    int64_t b = 2 * ((int64_t)shaped * config->shaper[0] + (int64_t)square * config->shaper[1]);
    if (b < 0) {
      b = 0;
    }
    if (b >= 0x0FF000000000LL) {
      b = 0x0FF000000000LL;
    }
    const int32_t brightness = gm2_store24(b);

    /* P:0x0010C0..0x0010C5: the lamp filament slewer P:0x001072, on the fall
     * poles while the target is not above the live brightness. */
    const int32_t lamp = state->lamp;
    if (gm2_word_acc(brightness) <= gm2_word_acc(lamp)) {
      a = 2 * ((int64_t)brightness * coef[GM2_UNIVIBE_CORE_LAMP_POLES + 0] +
               (int64_t)lamp * coef[GM2_UNIVIBE_CORE_LAMP_POLES + 1]);
    } else {
      a = 2 * ((int64_t)brightness * coef[GM2_UNIVIBE_CORE_LAMP_POLES + 2] +
               (int64_t)lamp * coef[GM2_UNIVIBE_CORE_LAMP_POLES + 3]);
    }
    state->lamp = gm2_store24(a);
    const int32_t around = gm2_store24(a * 8);

    /* P:0x0010C6..0x0010D4: the four all-pass targets, each a point of the
     * parabola 1 - coefficient^2 halved towards the live word. */
    for (unsigned k = 0; k < GM2_UNIVIBE_CORE_STAGES; k++) {
      const int32_t base =
          gm2_store24((int64_t)coef[GM2_UNIVIBE_CORE_CURVES + 2u * k + 1u] * 0x1000000 +
                      2 * (int64_t)around * coef[GM2_UNIVIBE_CORE_CURVES + 2u * k]);
      const int32_t product = gm2_store24(2 * (int64_t)base * base);
      b = 2 * (0x400000000000LL - 2 * (int64_t)product * base);
      if (b <= 0) {
        b = 0;
      }
      state->pole_step[k] = gm2_store24((b - gm2_word_acc(state->pole[k])) >> 2);
    }
  }

  /* P:0x0010D7..0x0010E1: the four live coefficients step towards their targets
   * on every frame. */
  const int32_t pole_0 = gm2_store24(gm2_word_acc(state->pole[0] + state->pole_step[0]));
  const int32_t pole_1 = gm2_store24(gm2_word_acc(state->pole[1] + state->pole_step[1]));
  const int32_t pole_2 = gm2_store24(gm2_word_acc(state->pole[2] + state->pole_step[2]));
  const int32_t pole_3 = gm2_store24(gm2_word_acc(state->pole[3] + state->pole_step[3]));
  state->pole[0] = pole_0;
  state->pole[1] = pole_1;
  state->pole[2] = pole_2;
  state->pole[3] = pole_3;

  /* P:0x0010E3..0x0010F3: the four cascaded all-pass stages. */
  int32_t stage = univibe_stage(&state->stage[0], state->stage[1], pole_0, wet);
  stage = univibe_stage(&state->stage[1], state->stage[2], pole_1, stage);
  stage = univibe_stage(&state->stage[2], state->stage[3], pole_2, stage);
  stage = univibe_stage(&state->stage[3], state->stage[4], pole_3, stage);
  state->stage[GM2_UNIVIBE_CORE_STAGES] = stage;

  /* P:0x0010F5..0x001109: the stages' word mixed with each channel's gained
   * input, then the output soft clip. */
  const int64_t left =
      2 * ((int64_t)coef[GM2_UNIVIBE_CORE_WET] * stage + (int64_t)UNIVIBE_MIX * gained_l);
  const int64_t right = 2 * (int64_t)UNIVIBE_MIX * (stage + gained_r);
  const int32_t right_mix = gm2_store24(gm2_asr_n(right, 2));
  const int32_t wet_l = gm2_store24(univibe_clip(gm2_asr_n(left, 2), k_output_clip));
  const int32_t wet_r = gm2_store24(univibe_clip(gm2_word_acc(right_mix), k_output_clip));

  /* P:0x00110D..0x001112: the wet word; the zero dry term's MACR still rounds. */
  out[0] = gm2_store24(gm2_mpyr(GM2_LEVEL_WET, wet_l));
  out[1] = gm2_store24(gm2_mpyr(wet_r, GM2_LEVEL_WET));

  if (++state->chunk_pos < GM2_CHUNK_FRAMES) {
    return;
  }
  state->chunk_pos = 0;
  univibe_jobs(state, config);
}

void gm2_univibe_core_state_init(gm2_univibe_core_state *state,
                                 const gm2_univibe_core_config *config) {
  state->lfo_speed = 0;
  state->lfo_phase = 0;
  state->lamp = 0;
  for (unsigned i = 0; i < GM2_UNIVIBE_CORE_STAGES + 1u; i++) {
    state->stage[i] = 0;
  }
  for (unsigned i = 0; i < GM2_UNIVIBE_CORE_STAGES; i++) {
    state->pole[i] = 0;
    state->pole_step[i] = 0;
  }
  for (unsigned i = 0; i < GM2_UNIVIBE_CORE_COEFS; i++) {
    state->coef[i] = config->coef[i];
  }
  for (unsigned i = 0; i < GM2_UNIVIBE_CORE_RAMPS; i++) {
    state->ramp_step[i] = 1;
    state->raw[i] = config->raw[i];
  }
  state->chunk_pos = 0;
}

void gm2_univibe_core_align(gm2_univibe_core_state *state, const gm2_univibe_core_config *config,
                            double beats) {
  if (config->note != 0u) {
    state->lfo_phase = gm2_lfo_phase_at(beats, config->note);
  }
}
