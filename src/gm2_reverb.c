#include "gm2_reverb.h"

#include "gm2_dsp_math.h"
#include "gm2_job.h"
#include "gm2_process.h"

#include <string.h>

/* No chain in the kernel reaches 2^52: the longest sums eleven doubled products
 * of two 24-bit words, and the matrix's seven times four stays below it. So the
 * sums run as plain int64 arithmetic: only the firmware's rounding and 24-bit
 * store points are left. */

/* The walk's offsets after the tank: the two the reset leaves the pointers on,
 * then the early taps', and last the pre-delay tap's distance. */
enum {
  REVERB_MATRIX_STEP = 21, /* the first of the five steps the matrix moves r5 by */
  REVERB_EARLY_FRONT = 20, /* what the reset moves r7 by */
  REVERB_EARLY_BACK = 26,  /* what it moves r5 by */
  REVERB_EARLY_STEP = 27,  /* the first early tap's */
  REVERB_PRE_DELAY = GM2_REVERB_OFFSETS - 1,
};

/* A pointer's move: its step word wraps in the buffer. */
static inline uint32_t reverb_step(uint32_t at, int32_t step) {
  return (at + (uint32_t)step) & (GM2_REVERB_BUFFER_WORDS - 1u);
}

/* The walk's two buffer pointers, r7 and r5, and the step they take next. */
typedef struct {
  uint32_t front;
  uint32_t back;
  const int32_t *step;
  const int32_t *last; /* the pre-delay tap's offset, which no pointer takes */
} reverb_pointers;

/* After the tank the pointers take the walk's offsets in turn. Past the last of
 * them a pointer stays put. */
static inline int32_t reverb_take(reverb_pointers *walk) {
  return walk->step < walk->last ? *walk->step++ : 0;
}

/* One early-reflection biquad, P:0x00137D..0x001393: a direct form whose 48-bit
 * accumulator carries the fraction of each output over to the next. */
static inline int32_t reverb_biquad(gm2_reverb_biquad *stage, const int32_t *c, int32_t x0) {
  const int64_t low = (uint32_t)stage->low & 0xFFFFFFu;
  int64_t a = 2 * (int64_t)c[0] * x0;
  a += 2 * (int64_t)stage->in[0] * c[1];
  a += 2 * (int64_t)stage->in[1] * c[2];
  stage->in[0] = stage->in[1];
  stage->in[1] = x0;
  a += 2 * (int64_t)c[3] * stage->out[0];
  a += 2 * (int64_t)stage->out[1] * c[4];
  stage->out[0] = stage->out[1];
  a = low + 2 * a;
  stage->low = gm2_lo24(a);
  stage->out[1] = gm2_store24(a);
  return stage->out[1];
}

/* P:0x0012E6..0x001363: the input's one-pole, the two diffusers, the seven tank
 * lines and the matrix that crosses their outputs into the tank's two sums. The
 * buffer reads and writes keep the kernel's order: a write of one line can land
 * on a word a later read takes.
 *
 * The pointers walk from the write point, each by its own run of the config's
 * offsets: r5 by 0, 2, 4 and 6, then 7 and every second one on, and by 21 to 25
 * in the matrix; r7 by 1, 3 and 5, then 8 and every second one on. The early
 * reflections then read on from where the reset leaves them. */
static void reverb_tank(gm2_reverb_state *state, const gm2_reverb_config *config, uint32_t origin) {
  int32_t *restrict buf = state->buffer;
  const int32_t *restrict c = state->coef;
  const int32_t *restrict o = config->offsets;
  uint32_t front = reverb_step(origin, o[1]);
  uint32_t back = reverb_step(origin, o[0]);

  /* 0x12E6..0x12F8: the pre-delayed input through the one-pole, then the two
   * diffusers, each an all-pass around one stretch of the buffer. */
  int32_t x1 = state->input_lp;
  int32_t y0 = c[GM2_REVERB_INPUT_LP];
  int64_t b = 2 * (int64_t)x1 * y0;
  int32_t x0 = state->pre_delay;
  y0 = c[GM2_REVERB_INPUT_LP + 1];
  b += 2 * (int64_t)y0 * x0;
  y0 = c[GM2_REVERB_INPUT_LP + 2];
  state->input_lp = gm2_store24(b);
  b += 2 * (int64_t)y0 * x0;
  y0 = c[GM2_REVERB_DIFFUSER];
  b = gm2_asr_n(b, 2);
  x0 = buf[back];
  back = reverb_step(back, o[2]);
  x1 = gm2_store24(b);
  b += 2 * (int64_t)y0 * x0;
  int32_t y1 = c[GM2_REVERB_DIFFUSER + 1];
  int64_t a = -2 * (int64_t)x1 * y0;
  buf[front] = gm2_store24(b);
  front = reverb_step(front, o[3]);
  a += 2 * (int64_t)x0 * y1;
  b = a;
  x0 = buf[back];
  back = reverb_step(back, o[4]);
  b += 2 * (int64_t)y0 * x0;
  x1 = gm2_store24(a);
  a = -2 * (int64_t)x1 * y0;
  a += 2 * (int64_t)x0 * y1;
  x0 = gm2_store24(b);
  int32_t word = buf[back];
  back = reverb_step(back, o[6]);
  y1 = gm2_store24(a);
  a = gm2_word_acc(x0);

  /* 0x12F9..0x133E: the seven lines. Each takes its sample out of the buffer,
   * runs it through its two damping bands, and writes the result, plus the
   * diffused input, where the next line starts. The first line takes its two
   * steps in the other order. */
  const int32_t *sign = config->tank_signs;
  int64_t taps = 0;
  for (unsigned k = 0; k < GM2_REVERB_TANK_LINES; k++) {
    const int32_t *dc = &c[GM2_REVERB_DAMPING + 6u * k];
    int32_t *const damp = state->damping[k];
    x0 = word;
    b = 2 * ((int64_t)damp[0] * dc[0] + (int64_t)dc[1] * x0);
    x1 = damp[1];
    taps += 2 * (int64_t)x0 * sign[2u * k + 1u];
    buf[front] = gm2_store24(a);
    front = reverb_step(front, o[k == 0u ? 5u : 6u + 2u * k]);
    const int32_t band0 = gm2_store24(b);
    damp[0] = band0;
    a = 2 * ((int64_t)x1 * dc[2] + (int64_t)dc[3] * x0);
    word = buf[back];
    back = reverb_step(back, o[7u + 2u * k]);
    damp[1] = gm2_store24(a);
    a += 2 * (int64_t)dc[4] * x0;
    a += gm2_word_acc(y1);
    a += 2 * (int64_t)band0 * dc[5];
  }

  /* 0x133F..0x1363: the last line's write, then the matrix: one sum takes the
   * lines' first taps, the other their second ones, read on from where the walk
   * stands, each with its sign. */
  y1 = word;
  buf[front] = gm2_store24(a);
  a = taps;
  b = 0;
  for (unsigned k = 0; k < GM2_REVERB_TANK_LINES; k++) {
    b += 2 * (int64_t)y1 * sign[2u * k];
    if (k + 1u < GM2_REVERB_TANK_LINES) {
      y1 = buf[back];
      if (k + 2u < GM2_REVERB_TANK_LINES) {
        back = reverb_step(back, o[REVERB_MATRIX_STEP + k]);
      }
    }
  }
  state->tank_out[0] = gm2_store24(4 * a);
  state->tank_out[1] = gm2_store24(4 * b);
}

/* P:0x001364..0x001394 and P:0x001395..0x0013C3: one channel's early signal.
 * Eight taps, read through the two pointers in turns, in three groups with the
 * config's weights; each group adds its own last sum through a comb feedback
 * word; and the groups' total runs through the channel's two biquads. */
static int32_t reverb_early(gm2_reverb_state *state, const gm2_reverb_config *config,
                            reverb_pointers *walk, unsigned channel) {
  const int32_t *restrict buf = state->buffer;
  const int32_t *restrict c = state->coef;
  const int32_t *restrict weight = config->early[channel];
  int32_t *const comb = state->comb[channel];
  const int32_t *const comb_coef = &c[GM2_REVERB_COMB + GM2_REVERB_EARLY_COMBS * channel];

  /* The taps turn from one pointer to the other, so each group's first tap is on
   * the one the group before it left. */
  int64_t sum = 2 * ((int64_t)weight[0] * buf[walk->front]);
  walk->front = reverb_step(walk->front, reverb_take(walk));
  sum += 2 * ((int64_t)weight[1] * buf[walk->back]);
  walk->back = reverb_step(walk->back, reverb_take(walk));
  sum += 2 * ((int64_t)weight[2] * buf[walk->front]);
  walk->front = reverb_step(walk->front, reverb_take(walk));
  sum += 2 * (int64_t)comb[0] * comb_coef[0];
  comb[0] = gm2_store24(sum);
  int64_t total = sum;

  sum = 2 * ((int64_t)weight[3] * buf[walk->back]);
  walk->back = reverb_step(walk->back, reverb_take(walk));
  sum += 2 * ((int64_t)weight[4] * buf[walk->front]);
  walk->front = reverb_step(walk->front, reverb_take(walk));
  sum += 2 * ((int64_t)weight[5] * buf[walk->back]);
  walk->back = reverb_step(walk->back, reverb_take(walk));
  sum += 2 * (int64_t)comb[1] * comb_coef[1];
  comb[1] = gm2_store24(sum);
  total += sum;

  sum = 2 * ((int64_t)weight[6] * buf[walk->front]);
  walk->front = reverb_step(walk->front, reverb_take(walk));
  sum += 2 * ((int64_t)weight[7] * buf[walk->back]);
  walk->back = reverb_step(walk->back, reverb_take(walk));
  sum += 2 * (int64_t)comb[2] * comb_coef[2];
  comb[2] = gm2_store24(sum);
  total += sum;

  /* 0x1395..0x13C3: the channel's two biquads, both from the same pair of words. */
  int32_t word = gm2_store24(total);
  const int32_t *biquad = &c[GM2_REVERB_BIQUAD + 5u * GM2_REVERB_EARLY_BIQUADS * channel];
  word = reverb_biquad(&state->biquad[channel][0], biquad, word);
  return reverb_biquad(&state->biquad[channel][1], biquad + 5u, word);
}

/* The chunk's job run: the eighty words ride one-pole jobs. A word already on its
 * staged value is settled, since the job then takes the staged word itself, so a
 * chunk with nothing to move returns here. */
static void reverb_jobs(gm2_reverb_state *state, const gm2_reverb_config *config) {
  if (memcmp(state->coef, config->coef, sizeof state->coef) == 0) {
    return;
  }
  for (unsigned i = 0; i < GM2_REVERB_COEFS; i++) {
    state->coef[i] = gm2_job_smooth(state->coef[i], config->coef[i], GM2_JOB_SMOOTH_STEP);
  }
}

/* One stereo frame, P:0x0012D2..0x0013DE, then the chunk's job run once its last
 * frame is done. */
static void reverb_frame(gm2_reverb_state *state, const gm2_reverb_config *config,
                         const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  const int32_t *c = state->coef;
  const int32_t *o = config->offsets;
  const uint32_t origin = state->write;

  /* 0x12D7..0x12E5: the mono input at the write point, where both pointers
   * start. */
  int64_t b = 2 * (int64_t)GM2_LEVEL_INPUT * gm2_s24((uint32_t)in[0]);
  b += 2 * (int64_t)gm2_s24((uint32_t)in[1]) * GM2_LEVEL_INPUT;
  state->buffer[origin] = gm2_store24(gm2_asr_n(b, 2));

  reverb_tank(state, config, origin);

  /* 0x1360..0x1363: both pointers go back to the write point, each keeping the
   * step it had coming, for the early reflections. */
  reverb_pointers walk = {
      .front = reverb_step(origin, o[REVERB_EARLY_FRONT]),
      .back = reverb_step(origin, o[REVERB_EARLY_BACK]),
      .step = &o[REVERB_EARLY_STEP],
      .last = &o[REVERB_PRE_DELAY],
  };
  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    state->early_out[channel] = reverb_early(state, config, &walk, channel);
  }

  /* 0x13B8..0x13C5: the pre-delay tap, and the write point a word on. */
  const int32_t pre_delay = state->buffer[reverb_step(origin, o[REVERB_PRE_DELAY])];
  state->write = (origin + 1u) & (GM2_REVERB_BUFFER_WORDS - 1u);

  /* 0x13C6..0x13DE: the Room and Reverb levels, the pre-delay tap's gain, and
   * the wet word; the zero dry term's MACR still rounds. */
  int64_t right = 2 * (int64_t)c[GM2_REVERB_ROOM] * state->early_out[1];
  int64_t left = 2 * (int64_t)c[GM2_REVERB_ROOM + 1] * state->early_out[0];
  right += 2 * (int64_t)c[GM2_REVERB_LEVEL] * state->tank_out[1];
  left += 2 * (int64_t)c[GM2_REVERB_LEVEL + 1] * state->tank_out[0];
  state->pre_delay = gm2_store24(2 * (int64_t)c[GM2_REVERB_PRE_DELAY] * pre_delay);
  out[0] = gm2_store24(gm2_round24(gm2_asl(2 * (int64_t)GM2_LEVEL_WET * gm2_store24(left))));
  out[1] = gm2_store24(gm2_round24(gm2_asl(2 * (int64_t)GM2_LEVEL_WET * gm2_store24(right))));

  if (++state->chunk_pos < GM2_CHUNK_FRAMES) {
    return;
  }
  state->chunk_pos = 0;
  reverb_jobs(state, config);
}

void gm2_reverb_state_init(gm2_reverb_state *state, const gm2_reverb_config *config) {
  memset(state, 0, sizeof *state);
  for (unsigned i = 0; i < GM2_REVERB_COEFS; i++) {
    state->coef[i] = config->coef[i];
  }
}

GM2_PROCESS_DEFINE(reverb, reverb_frame)
