#include "gm2_delay_core.h"

#include "gm2_dsp_math.h"
#include "gm2_process.h"

#include <string.h>

/* One tap's read, P:0x001214: the word the line's offset behind the write point
 * picks, and the tap's own Reverse Delay walk. */
static inline int32_t delay_tap(const gm2_delay_core_state *state, gm2_delay_core_line *line,
                                unsigned t) {
  int32_t idx = (int32_t)state->write + line->tap_offset[t] + line->rev_offset[t];
  if (idx >= (int32_t)GM2_DELAY_CORE_BUFFER_WORDS) {
    idx -= (int32_t)GM2_DELAY_CORE_BUFFER_WORDS;
  }
  int32_t next_rev = line->rev_offset[t] + state->rev_step;
  if (next_rev >= (int32_t)GM2_DELAY_CORE_BUFFER_WORDS) {
    next_rev -= (int32_t)GM2_DELAY_CORE_BUFFER_WORDS;
  }
  line->rev_offset[t] = next_rev;
  return state->buffer[(uint32_t)idx];
}

/* One line's two reads, the blend of their taps and its LP/HP pair, at
 * P:0x001214..0x001231. Every sum is one int64 expression: a doubled product of
 * two 24-bit words is at most 2^47, so these three-term sums stay below 2^49 and
 * only the two MACR rounding points and the 24-bit stores follow the firmware.
 * Each filter pairs the previous and the current word of one state at a single
 * coefficient, which is what makes two multiplies enough. */
static inline void delay_line(gm2_delay_core_state *state, unsigned n) {
  gm2_delay_core_line *line = &state->line[n];
  const int32_t s0 = delay_tap(state, line, 0u);
  const int32_t s1 = delay_tap(state, line, 1u);

  const int32_t *fc = &state->coef[GM2_DELAY_CORE_FILTER + 4u * n];
  const int32_t u_prev = line->filter[0];
  const int32_t v_prev = line->filter[1];
  const int32_t w_prev = line->filter[2];
  const int32_t u_cur =
      gm2_store24(2 * ((int64_t)line->tap_gain[0] * s0 + (int64_t)line->tap_gain[1] * s1));
  const int64_t pre = 2 * ((int64_t)fc[1] * u_prev - (int64_t)v_prev * fc[0]);
  const int32_t v_cur = gm2_store24(gm2_round24(pre + 2 * (int64_t)fc[1] * u_cur));
  const int64_t pre_hp = -2 * (int64_t)fc[2] * w_prev - 2 * (int64_t)v_prev * fc[3];
  const int32_t w_cur = gm2_store24(gm2_round24(pre_hp + 2 * (int64_t)v_cur * fc[3]));
  line->filter[0] = u_cur;
  line->filter[1] = v_cur;
  line->filter[2] = w_cur;
  line->out = w_cur;
}

int gm2_delay_core_process_frame(gm2_delay_core_state *state,
                                 const int32_t coef[GM2_DELAY_CORE_COEFS],
                                 const int32_t *tap_offset, const int32_t in[GM2_CHANNELS],
                                 int32_t out[GM2_CHANNELS]) {
  const int32_t *c = state->coef;
  const int32_t in_l = gm2_s24((uint32_t)in[0]);
  const int32_t in_r = gm2_s24((uint32_t)in[1]);
  const int32_t x0_in = gm2_store24(gm2_mpyr(GM2_LEVEL_INPUT, in_l));
  const int32_t x1_in = gm2_store24(gm2_mpyr(GM2_LEVEL_INPUT, in_r));
  const int32_t line0 = state->line[0].out;
  const int32_t line1 = state->line[1].out;

  const int32_t y0_wr = gm2_store24(2 * ((int64_t)c[GM2_DELAY_CORE_INPUT + 0] * x0_in +
                                         (int64_t)x1_in * c[GM2_DELAY_CORE_INPUT + 2] +
                                         (int64_t)c[GM2_DELAY_CORE_FEEDBACK + 1] * line0 +
                                         (int64_t)line1 * c[GM2_DELAY_CORE_FEEDBACK + 3]));
  const int32_t b_wr = gm2_store24(2 * ((int64_t)x0_in * c[GM2_DELAY_CORE_INPUT + 1] +
                                        (int64_t)c[GM2_DELAY_CORE_INPUT + 3] * x1_in +
                                        (int64_t)line1 * c[GM2_DELAY_CORE_FEEDBACK + 0] +
                                        (int64_t)c[GM2_DELAY_CORE_FEEDBACK + 2] * line0));

  /* The firmware wraps at its base address or below it, and the two-word step means
   * the relative pointer lands on 0 or underflows to the 24-bit -1. */
  if ((int32_t)state->write <= 0) {
    state->write += GM2_DELAY_CORE_BUFFER_WORDS;
  }
  state->buffer[state->write] = b_wr;
  state->write--;
  state->buffer[state->write] = y0_wr;
  state->write--;

  delay_line(state, 0u);
  delay_line(state, 1u);

  const int32_t wet_l =
      gm2_store24(2 * ((int64_t)c[GM2_DELAY_CORE_OUTPUT + 1] * state->line[0].out +
                       (int64_t)state->line[1].out * c[GM2_DELAY_CORE_OUTPUT + 3]));
  const int32_t wet_r =
      gm2_store24(2 * ((int64_t)state->line[1].out * c[GM2_DELAY_CORE_OUTPUT + 0] +
                       (int64_t)c[GM2_DELAY_CORE_OUTPUT + 2] * state->line[0].out));
  /* The wet word; the zero dry term's MACR still rounds. */
  out[0] = gm2_store24(gm2_mpyr(GM2_LEVEL_WET, wet_l));
  out[1] = gm2_store24(gm2_mpyr(wet_r, GM2_LEVEL_WET));

  if (++state->chunk_pos < GM2_CHUNK_FRAMES) {
    return 0;
  }
  state->chunk_pos = 0;
  /* P:0x0001FD on Y+0..19. */
  for (unsigned i = 0; i < GM2_DELAY_CORE_COEFS; i++) {
    state->coef[i] = gm2_job_smooth(state->coef[i], coef[i], GM2_JOB_SMOOTH_STEP);
  }
  /* P:0x000301 and P:0x000293 a line: the settled check on the staged offset,
   * then one step of the crossfade. */
  for (unsigned n = 0; n < GM2_DELAY_CORE_LINES; n++) {
    gm2_delay_core_line *line = &state->line[n];
    if (tap_offset != NULL) {
      (void)gm2_job_fade_check(&line->fade, line->tap_offset, tap_offset[n]);
    }
    gm2_job_fade_step(&line->fade, line->tap_gain);
  }
  return 1;
}

void gm2_delay_core_state_init(gm2_delay_core_state *state,
                               const int32_t coef[GM2_DELAY_CORE_COEFS],
                               const int32_t tap_offset[GM2_DELAY_CORE_LINES], int32_t rev_step) {
  memset(state, 0, sizeof *state);
  state->write = GM2_DELAY_CORE_BUFFER_WORDS - 1u;
  for (unsigned i = 0; i < GM2_DELAY_CORE_COEFS; i++) {
    state->coef[i] = coef[i];
  }
  state->rev_step = rev_step;
  for (unsigned n = 0; n < GM2_DELAY_CORE_LINES; n++) {
    gm2_delay_core_line *line = &state->line[n];
    line->tap_offset[0] = tap_offset[n];
    line->tap_offset[1] = tap_offset[n];
    gm2_job_fade_init(&line->fade, line->tap_gain);
  }
}
