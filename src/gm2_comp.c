#include "gm2_comp.h"

#include "gm2_dsp_math.h"
#include "gm2_job.h"
#include "gm2_process.h"
#include "gm2_tables.h"

/* Y:0x56B, which P:0x094CFA installs, and the knee curvature P:0x00129E carries
 * as an immediate. */
enum {
  COMP_KNEE_HALF = 0x03526A,
  COMP_KNEE_CURVE = 0x4D104A,
};

/* 0x1282..0x12CF, the gain computer: log2 of the detector level, the threshold,
 * the knee and the ratio, a target that creeps up by one release step while the
 * gain recovers, a coefficient between the two outer words of the triple that
 * recovery or fall selects, the smoothed 48-bit log2 gain and its makeup, and exp2
 * back into the gain word. */
static void comp_gain(gm2_comp_state *state) {
  const int32_t *coef = state->coef;

  /* 0x1282..0x1290: log2 through CLB, NORMF and the shared table pair. */
  int64_t acc = gm2_log2(state->level);

  /* 0x1291..0x1299: half the level, less the threshold, against the knee. */
  acc = gm2_asr_n(acc, 1);
  acc = gm2_sub24(acc, coef[GM2_COMP_THRESHOLD]);
  acc = gm2_neg56(acc);
  const int64_t knee = gm2_word_acc(COMP_KNEE_HALF);
  if (acc >= knee) {
    acc = 0;
  } else {
    acc = gm2_neg56(acc);
    if (acc >= knee) {
      acc = 2 * (int64_t)coef[GM2_COMP_RATIO] * gm2_store24(acc);
    } else {
      const int32_t bend = gm2_store24(acc + gm2_word_acc(COMP_KNEE_HALF));
      const int32_t square = gm2_store24(2 * (int64_t)bend * bend);
      acc = 2 * (int64_t)coef[GM2_COMP_RATIO] * square;
      acc = 2 * (int64_t)COMP_KNEE_CURVE * gm2_store24(acc);
      acc = gm2_asl_n(acc, 4);
    }
  }

  /* 0x12A6..0x12AE: the target climbs by one recovery step while the gain has to
   * recover, and jumps to the wanted gain while it falls. The conditional add
   * leaves the condition codes alone, so 0x12AB's LE is still 0x12A9's. 0x12AB's
   * address transfer is conditional too: a recovering chunk keeps reading the
   * release triple at Y:0x56E, a falling one moves on to the attack triple at
   * Y:0x571. */
  const int32_t log_high = state->log_gain[0];
  int32_t target = state->target;
  const int32_t wanted = gm2_store24(acc);
  const int64_t over = acc - gm2_word_acc(log_high);
  const int32_t *smooth;
  if (over > 0) {
    target = gm2_store24(gm2_add24(gm2_word_acc(target), coef[GM2_COMP_RELEASE_STEP]));
    smooth = &coef[GM2_COMP_RELEASE];
  } else {
    target = wanted;
    smooth = &coef[GM2_COMP_ATTACK];
  }

  /* 0x12AC..0x12B6: the smoothing coefficient between the two outer words of that
   * triple, shaped by the squared gap to the last gain word. */
  const int32_t gap = gm2_store24(over);
  acc = 2 * (int64_t)smooth[0] * gm2_store24(2 * (int64_t)gap * gap);
  acc = gm2_asl_n(acc, 15);
  acc = gm2_add24(acc, smooth[1]);
  if (acc > gm2_word_acc(smooth[1])) {
    acc = gm2_word_acc(smooth[1]);
  }
  if (acc < gm2_word_acc(smooth[2])) {
    acc = gm2_word_acc(smooth[2]);
  }
  state->target = target;

  /* 0x12B7..0x12BF: the 48-bit log2 gain a step towards the target, then the
   * makeup gain added to its high word. */
  acc -= gm2_word_acc(GM2_W24_MAX);
  const int32_t coeff = gm2_store24(acc);
  /* The pair is one word above the other, so the low half ORs in. */
  const int64_t log_gain = gm2_word_acc(log_high) | ((uint32_t)state->log_gain[1] & 0xFFFFFFu);
  const int32_t reach = gm2_store24(gm2_word_acc(target) - log_gain);
  acc = log_gain - 2 * (int64_t)reach * coeff;
  state->log_gain[1] = (int32_t)((uint32_t)acc & 0xFFFFFFu);
  state->log_gain[0] = gm2_store24(acc);
  acc += gm2_word_acc(coef[GM2_COMP_GAIN]);

  /* 0x12C1..0x12CF: exp2 of the gain word out of the second table. */
  const int32_t gain_word = gm2_store24(acc);
  int64_t table = gm2_exp2(gain_word);
  state->gain = gm2_store24(table);
}

/* The chunk's job run. P:0x094D5C stages and arms Threshold, Ratio and Gain
 * together, on every turn of any one of their knobs and whatever the turn leaves
 * the staged words. The other eight words ride one-pole jobs. */
static void comp_jobs(gm2_comp_state *state, const gm2_comp_config *config) {
  int armed = 0;
  for (unsigned i = 0; i < GM2_COMP_RAMPS; i++) {
    armed |= config->raw[i] != state->raw[i];
    state->raw[i] = config->raw[i];
  }
  for (unsigned i = 0; i < GM2_COMP_RAMPS; i++) {
    if (armed) {
      state->ramp_step[i] = gm2_job_ramp_arm(state->coef[i], config->coef[i]);
    }
    state->coef[i] = gm2_job_ramp(state->coef[i], config->coef[i], state->ramp_step[i]);
  }
  for (unsigned i = GM2_COMP_RAMPS; i < GM2_COMP_COEFS; i++) {
    state->coef[i] = gm2_job_smooth(state->coef[i], config->coef[i], GM2_JOB_SMOOTH_STEP);
  }
}

/* One stereo frame, P:0x00125E..0x001281, then the chunk's job run once its last
 * frame is done. */
static void comp_frame(gm2_comp_state *state, const gm2_comp_config *config,
                       const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  const int32_t detector = state->coef[GM2_COMP_DETECTOR];

  /* 0x1263..0x1265: the input gain, one MPYR per channel. */
  const int32_t left_word = gm2_store24(gm2_mpyr(GM2_LEVEL_INPUT, gm2_s24((uint32_t)in[0])));
  const int32_t right_word = gm2_store24(gm2_mpyr(gm2_s24((uint32_t)in[1]), GM2_LEVEL_INPUT));

  /* 0x1266: MAXM keeps the louder channel. Both accumulators are the words
   * already rounded, within 2^23 in magnitude, so the words compare the same. */
  const int32_t detect =
      (left_word < 0 ? -left_word : left_word) > (right_word < 0 ? -right_word : right_word)
          ? left_word
          : right_word;

  /* 0x1269..0x1273: square the louder word into the 48-bit detector level
   * through a one-pole. The squared word the firmware leaves at X:0x378 for the
   * output stage is the pre-instruction accumulator, the left channel after the
   * input gain. */
  const int64_t level = state->level;
  const int64_t squared = 2 * (int64_t)detect * detect;
  const int64_t over = squared - level;
  const uint32_t low = (uint32_t)over & 0xFFFFFFu;
  const int32_t high = gm2_store24(over);
  int64_t follow = gm2_dmacsu(0, detector, low);
  follow = gm2_dmacss(follow, detector, high);
  state->level = gm2_store48(follow + level);

  /* 0x1274..0x1279: the gain word on both channels with five bits of headroom. */
  const int32_t gain = state->gain;
  const int64_t wet_right = gm2_mpyr(right_word, gain) * 32;
  const int32_t wet_left = gm2_store24(gm2_mpyr(gain, left_word) * 32);

  /* 0x127A..0x127F: the wet word; the zero dry term's MACR still rounds. */
  out[0] = gm2_store24(gm2_round24(2 * (int64_t)GM2_LEVEL_WET * wet_left));
  out[1] = gm2_store24(gm2_round24(2 * (int64_t)gm2_store24(wet_right) * GM2_LEVEL_WET));

  /* 0x1280: the computer runs on the first frame of each chunk. */
  if (state->chunk_pos == 0u) {
    comp_gain(state);
  }
  if (++state->chunk_pos < GM2_CHUNK_FRAMES) {
    return;
  }
  state->chunk_pos = 0;
  comp_jobs(state, config);
}

void gm2_comp_state_init(gm2_comp_state *state, const gm2_comp_config *config) {
  state->level = 0;
  state->gain = 0;
  state->log_gain[0] = 0;
  state->log_gain[1] = 0;
  state->target = 0;
  for (unsigned i = 0; i < GM2_COMP_COEFS; i++) {
    state->coef[i] = config->coef[i];
  }
  for (unsigned i = 0; i < GM2_COMP_RAMPS; i++) {
    state->ramp_step[i] = 1;
    state->raw[i] = config->raw[i];
  }
  state->chunk_pos = 0;
}

GM2_PROCESS_DEFINE(comp, comp_frame)
