#include "gm2_gate.h"

#include "gm2_dsp_math.h"
#include "gm2_gate_detector.h"
#include "gm2_job.h"
#include "gm2_process.h"
#include "gm2_tables.h"

#include <string.h>

/* The block constants P:0x090441 writes to Y:0x373..0x384. */
enum {
  GATE_UNITY = 0x7FFFFF,     /* Y:0x37F and Y:0x384 */
  GATE_KNEE_HALF = 0x010000, /* the knee's half width */
  GATE_EXP_C0 = -0x4D1045,   /* 0xB2EFBB as a signed word */
  GATE_EXP_C1 = 0x566DF8,
};

/* 0xC69..0xCBD, the gain computer: log2 of the envelope, the knee, the slewed
 * target and the smoothed gain, then exp2 for the next frame's coefficients.
 * `envelope` is the frame's accumulator, before its 24-bit store. */
static void gate_gain(gm2_gate_state *state, int64_t envelope) {
  const int32_t mode = state->coef[GM2_GATE_MODE];
  const int32_t *release = &state->coef[GM2_GATE_RELEASE];

  /* 0xC6A..0xC76: log2 through CLB, NORMF and the shared table pair. */
  int64_t level = gm2_log2(envelope);

  /* 0xC7A..0xC8D: the level over the threshold, and the knee's curvature. */
  level = gm2_sub24(level, state->coef[GM2_GATE_THRESHOLD]);
  int64_t knee = gm2_word_acc(GATE_KNEE_HALF);
  if (level >= knee) {
    level = 0;
  } else {
    level = gm2_neg56(level);
    if (level < knee) {
      int32_t bend = gm2_store24(gm2_add24(level, GATE_KNEE_HALF));
      bend = gm2_store24(gm2_mpy(bend, bend));
      level = gm2_asl_n(gm2_mpy(-mode, bend), 9);
    } else {
      level = gm2_asl_n(gm2_mpy(-mode, gm2_store24(level)), 4);
    }
  }

  /* 0xC8E..0xC97: clamp to the damping floor, then slew the target down by one
   * release step while the gain has to fall. */
  int32_t gain = state->gain;
  int64_t floor = gm2_word_acc(state->coef[GM2_GATE_DAMPING]);
  int64_t target = gm2_word_acc(state->target);
  if (level < floor) {
    level = floor;
  }
  int64_t drop = gm2_sub24(level, gain);
  /* 0xC92 runs only while the gain has to fall, and a conditional instruction
   * leaves the condition codes alone, so 0xC93's GE is still 0xC91's. */
  if (drop < 0) {
    target = gm2_sub24(target, release[0]);
  } else {
    target = gm2_word_acc(gm2_store24(level));
  }
  if (target < floor) {
    target = floor;
  }
  state->target = gm2_store24(target);

  /* 0xC98..0xCA5: a release coefficient between the two outer words, and the
   * gain word moving towards the target by it. */
  int64_t rate = gm2_mac(gm2_word_acc(release[2]), gm2_store24(gm2_asl_n(drop, 5)), release[1]);
  if (rate > gm2_word_acc(release[2])) {
    rate = gm2_word_acc(release[2]);
  }
  if (rate < gm2_word_acc(release[3])) {
    rate = gm2_word_acc(release[3]);
  }
  rate = gm2_sub24(rate, GATE_UNITY);
  int32_t reach = gm2_store24(gm2_sub24(target, gain));
  int64_t gain_acc = gm2_add24(gm2_mpy(-reach, gm2_store24(rate)), gain);
  if (gain_acc > 0) {
    gain_acc = 0;
  }
  gain_acc = gm2_dec(gain_acc);
  state->gain = gm2_store24(gain_acc);

  /* 0xCA6..0xCB3: exp2 of the gain word out of the second table. */
  int32_t gain_word = gm2_store24(gain_acc);
  int64_t table = gm2_exp2(gain_word);

  /* 0xCB4..0xCBD: the two one-pole coefficients, used from the next frame on.
   * The parallel moves read the accumulator before their own instruction, so the
   * first word is stored ahead of the unity subtraction. */
  int32_t shape = gm2_store24(gm2_asl_n(gm2_mpy(GATE_EXP_C0, gain_word), 6));
  int64_t c0 = gm2_mpy(GATE_EXP_C1, shape);
  int32_t norm = gm2_store24(table);
  if (c0 < 0) {
    c0 = 0;
  }
  state->pole[0] = gm2_store24(c0);
  c0 = gm2_sub24(c0, GATE_UNITY);
  state->pole[1] = gm2_store24(gm2_mpy(-gm2_store24(c0), norm));
}

/* The chunk's job run. P:0x0904E8 arms Mode's ramp on every turn of that knob,
 * P:0x090513 Damping's, and P:0x090555 arms Threshold's before it calls
 * P:0x090513, so a Threshold turn arms both; each arms whatever the turn leaves
 * the staged word. The four Release words share one one-pole job.
 *
 * A run with every live word on its staged one and every raw word on the one a
 * turn left last moves nothing: a ramp there finds a zero gap within its step and
 * a one-pole job a zero half gap, and both take the staged word itself. */
static void gate_jobs(gm2_gate_state *state, const gm2_gate_config *config) {
  if (memcmp(state->coef, config->coef, sizeof state->coef) == 0 &&
      memcmp(state->raw, config->raw, sizeof state->raw) == 0) {
    return;
  }
  const int threshold = config->raw[GM2_GATE_THRESHOLD] != state->raw[GM2_GATE_THRESHOLD];
  const int armed[GM2_GATE_RAMPS] = {
      [GM2_GATE_MODE] = config->raw[GM2_GATE_MODE] != state->raw[GM2_GATE_MODE],
      [GM2_GATE_THRESHOLD] = threshold,
      [GM2_GATE_DAMPING] =
          threshold || config->raw[GM2_GATE_DAMPING] != state->raw[GM2_GATE_DAMPING],
  };
  for (unsigned i = 0; i < GM2_GATE_RAMPS; i++) {
    state->raw[i] = config->raw[i];
    if (armed[i]) {
      state->ramp_step[i] = gm2_job_ramp_arm(state->coef[i], config->coef[i]);
    }
    state->coef[i] = gm2_job_ramp(state->coef[i], config->coef[i], state->ramp_step[i]);
  }
  for (unsigned i = GM2_GATE_RAMPS; i < GM2_GATE_COEFS; i++) {
    state->coef[i] = gm2_job_smooth(state->coef[i], config->coef[i], GM2_JOB_SMOOTH_STEP);
  }
}

/* One stereo frame, P:0x000C4C..0x000C66, then the chunk's job run once its
 * last frame is done. Each of the frame's sums is at most two doubled products of
 * 24-bit words, below 2^48, so none needs the 56-bit wrap. */
static void gate_frame(gm2_gate_state *state, const gm2_gate_config *config,
                       const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  /* 0xC4C..0xC4E: the input gain. */
  int32_t gained[GM2_CHANNELS];
  gm2_gate_detector_gain(gained, in);

  /* 0xC4F..0xC53: one pole per channel. */
  int64_t a =
      2 * ((int64_t)state->pole[0] * state->filter[0] + (int64_t)state->pole[1] * gained[0]);
  int64_t b =
      2 * ((int64_t)state->pole[0] * state->filter[1] + (int64_t)gained[1] * state->pole[1]);
  state->filter[0] = gm2_store24(a);
  state->filter[1] = gm2_store24(b);

  /* 0xC56..0xC5D: the wet path is a plain MPY and a 24-bit store. */
  out[0] = gm2_store24(2 * (int64_t)GM2_LEVEL_WET * state->filter[0]);
  out[1] = gm2_store24(2 * (int64_t)state->filter[1] * GM2_LEVEL_WET);

  /* 0xC5E..0xC66: the detector's instant attack and one-pole decay. */
  int64_t envelope = gm2_gate_detector_follow(&state->envelope, gained);

  /* 0xC67: the computer runs on the first frame of each chunk. */
  if (state->chunk_pos == 0u) {
    gate_gain(state, envelope);
  }
  if (++state->chunk_pos < GM2_CHUNK_FRAMES) {
    return;
  }
  state->chunk_pos = 0;
  gate_jobs(state, config);
}

void gm2_gate_state_init(gm2_gate_state *state, const gm2_gate_config *config) {
  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    state->filter[channel] = 0;
  }
  state->envelope = 0;
  state->gain = 0;
  state->target = 0;
  state->pole[0] = 0;
  state->pole[1] = 0;
  for (unsigned i = 0; i < GM2_GATE_COEFS; i++) {
    state->coef[i] = config->coef[i];
  }
  for (unsigned i = 0; i < GM2_GATE_RAMPS; i++) {
    state->ramp_step[i] = 1;
    state->raw[i] = config->raw[i];
  }
  state->chunk_pos = 0;
}

GM2_PROCESS_DEFINE(gate, gate_frame)
