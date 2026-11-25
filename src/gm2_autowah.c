#include "gm2_autowah.h"

#include "gm2_dsp_math.h"
#include "gm2_job.h"
#include "gm2_lfo.h"
#include "gm2_process.h"
#include "gm2_tables.h"

/* The triangle's fixed one-pole, and the LFO's word at phase 0: the triangle's
 * (0 + 0x200000) << 1 and the sine table's first entry. The pole and its rest sum
 * to one, so a smoother holding that word keeps the triangle there. */
enum {
  AUTOWAH_TRI_POLE = 0x7FB5C8,
  AUTOWAH_TRI_REST = 0x004A38,
  AUTOWAH_START = 0x400000,
};

/* 0x0003D6..0x0003EE: the triangle in three regions of the phase, doubled and then
 * smoothed by the fixed one-pole, whose result fills both X:0x2BD and X:0x2BE. */
static void autowah_triangle(gm2_autowah_state *state) {
  int64_t a = gm2_word_acc(state->lfo_phase);
  int64_t b;
  if (a < gm2_word_acc(0x200000)) {
    /* TFR moves the field, and the phase's low field is always clear. */
    b = a;
  } else {
    b = gm2_word_acc(GM2_W24_MAX);
    if (a < gm2_word_acc(0x600000)) {
      b = gm2_asr_n(b, 1);
    } else {
      a = gm2_neg56(a);
      b = gm2_neg56(b);
    }
    b = gm2_acc56(b - a);
  }
  b = gm2_add24(b, 0x200000);
  b = gm2_asl(b);
  int32_t raw = gm2_store24(b);
  int64_t smoothed = gm2_mac(gm2_mpy(AUTOWAH_TRI_POLE, state->lfo_smooth), raw, AUTOWAH_TRI_REST);
  state->lfo_smooth = gm2_store24(smoothed);
  state->lfo_out = state->lfo_smooth;
}

/* P:0x0003BE, once a chunk: the phase advances by the live step, and the shape
 * picks the triangle or the interpolated sine table (0x0003C7..0x0003D4). */
static void autowah_lfo(gm2_autowah_state *state, const gm2_autowah_config *config) {
  state->lfo_phase = gm2_lfo_advance(state->lfo_phase, state->lfo_step);
  if (config->shape == 0) {
    autowah_triangle(state);
  } else {
    state->lfo_out = gm2_wave_read(gm2_lfo_sine_table, state->lfo_phase);
  }
}

/* One frame: the LFO on the chunk's first frame (0xF55..0xF58), the routine, the
 * step's job, then the tick once its period of chunks is over. */
static void autowah_frame(gm2_autowah_state *state, const gm2_autowah_config *config,
                          const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  if (state->core.chunk_pos == 0u) {
    autowah_lfo(state, config);
  }
  if (gm2_wah_core_process_frame(&state->core, config->shift, in, out) == 0) {
    return;
  }
  state->lfo_step = gm2_job_smooth(state->lfo_step, config->lfo_step, GM2_JOB_SMOOTH_STEP);
  if (++state->tick < config->tick_chunks) {
    return;
  }
  state->tick = 0;
  /* P:0x092541 as subtype 2 answers it: the sweep word takes the LFO's output. */
  state->core.sweep = state->lfo_out;
}

void gm2_autowah_state_init(gm2_autowah_state *state, const gm2_autowah_config *config) {
  gm2_wah_core_state_init(&state->core, AUTOWAH_START);
  state->lfo_phase = 0;
  state->lfo_smooth = AUTOWAH_START;
  state->lfo_out = AUTOWAH_START;
  state->lfo_step = config->lfo_step;
  state->tick = 0;
}

GM2_PROCESS_DEFINE(autowah, autowah_frame)

void gm2_autowah_align(gm2_autowah_state *state, const gm2_autowah_config *config, double beats) {
  if (config->note != 0u) {
    state->lfo_phase = gm2_lfo_phase_at(beats, config->note);
  }
}
