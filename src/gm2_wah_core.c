#include "gm2_wah_core.h"

#include "gm2_dsp_math.h"
#include "gm2_job.h"
#include "gm2_process.h"

/* Y:0x403..Y:0x406: the cubic that turns the sweep word into the staged
 * frequency, bias + sweep * (w0 + sweep * (w3 + sweep * w2)). */
enum {
  WAH_CUBIC_SWEEP = 0x1B3813,
  WAH_CUBIC_BIAS = 0x0D0AFC,
  WAH_CUBIC_CUBE = 0x1319D3,
  WAH_CUBIC_SQUARE = 0x1D5214,
};

/* Y:0x40D: the scale the filtered word is multiplied by before the clip. */
enum { WAH_DAMPING = 0x64F41F };

/* The left shift the routine applies after the soft clip. */
enum { WAH_CLIP_SHIFT = 3 };

/* The high and low words of the routine's 48-bit long registers L:0x00E..0x011. */
static inline int32_t wah_high(int64_t value) { return (int32_t)(value >> 24); }
static inline uint32_t wah_low(int64_t value) { return (uint32_t)value & 0xFFFFFFu; }

/* 0xF4D..0xF54: the sweep word through the cubic. The square and the sweep's
 * product with the cube coefficient enter through their 24-bit fields. */
static int32_t wah_frequency(int32_t sweep) {
  int64_t square = gm2_mpy(sweep, sweep);
  int32_t square_word = gm2_store24(square);
  int64_t cube = gm2_mpy(sweep, WAH_CUBIC_CUBE);
  int32_t cube_word = gm2_store24(cube);
  int64_t b = gm2_word_acc(WAH_CUBIC_BIAS);
  b = gm2_mac(b, sweep, WAH_CUBIC_SWEEP);
  b = gm2_mac(b, square_word, WAH_CUBIC_SQUARE);
  b = gm2_mac(b, square_word, cube_word);
  return gm2_store24(b);
}

/* 0xF62..0xF95, one channel: the double-precision state-variable filter at the
 * live frequency word, then the inlined soft clip and its left shift by 3. Every
 * sum below is a doubled product plus a 48-bit state, so it stays under 2^50 and
 * the 56-bit accumulator cannot wrap; only the stores limit. */
static int32_t wah_filter(gm2_wah_core_state *state, int32_t scratch, int32_t shifted,
                          int32_t quarter, unsigned channel) {
  /* 0xF6E..0xF75: the first state's feedback word, less the second state and plus
   * the input term. */
  const int64_t first = state->svf[channel][0];
  int64_t b = 2 * (int64_t)quarter * wah_low(first);
  b = gm2_dmacss(b, -quarter, wah_high(first));
  b = b * 4 - state->svf[channel][1] + gm2_word_acc(scratch);

  /* 0xF76..0xF7B: the second state's word, taken from the fields of that sum before
   * its 24-bit limit, plus the state the routine loaded before the store. */
  const uint32_t raw_low = wah_low(b);
  const int32_t raw_high = gm2_store24(b);
  b = 2 * (int64_t)shifted * raw_low;
  b = gm2_dmacss(b, shifted, raw_high);
  b += first;

  /* 0xF7C..0xF7D: the first state takes that sum. */
  state->svf[channel][0] = gm2_store48(b);

  /* 0xF7E..0xF82: the same word again from the limited state the store wrote, then
   * the second state takes it. */
  const int64_t limited = state->svf[channel][0];
  b = 2 * (int64_t)shifted * wah_low(limited);
  b = gm2_dmacss(b, shifted, wah_high(limited));
  state->svf[channel][1] = gm2_store48(state->svf[channel][1] + b);

  /* 0xF83..0xF95: the filtered word, the inlined soft clip and its shift by 3. */
  const int64_t damped = 2 * (int64_t)WAH_DAMPING * wah_high(limited) * 8;
  return gm2_store24(gm2_soft_clip(damped, gm2_soft_clip_words, WAH_CLIP_SHIFT));
}

int gm2_wah_core_process_frame(gm2_wah_core_state *state, int32_t shift, int32_t wet, int32_t dry,
                               const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  /* 0xF4D..0xF54, on the first frame of each chunk: the sweep word into the
   * frequency's target. */
  if (state->chunk_pos == 0u) {
    state->freq_target = wah_frequency(state->sweep);
  }

  /* 0xF62..0xF6D: the frequency down by the Range word and by two more, the
   * latter two as the per-frame scratch words, which both channels share. */
  const int32_t shifted =
      gm2_store24(gm2_asr_n(gm2_word_acc(state->freq), (unsigned)shift & 0x3Fu));
  const int32_t quarter = gm2_store24(gm2_asr_n(gm2_word_acc(state->freq), 2));

  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    /* 0xF29..0xF2D: the input gain, and its eighth for the filter's input term. */
    const int32_t gained = gm2_store24(gm2_mpyr(GM2_LEVEL_INPUT, gm2_s24((uint32_t)in[channel])));
    const int32_t scratch = gm2_store24(2 * (int64_t)(gained >> 3) * state->freq);
    const int32_t filtered = wah_filter(state, scratch, shifted, quarter, channel);

    /* 0xF96..0xFA3: the wet word and the filtered sample, the dry word and the
     * frame's input sample, one rounding over the sum. Each doubled product is at
     * most 2^47, so the 56-bit accumulator cannot wrap. */
    const int64_t sampled = 2 * (int64_t)state->dry * gm2_s24((uint32_t)in[channel]);
    out[channel] = gm2_store24(gm2_round24(2 * (int64_t)state->wet * filtered + sampled));
  }

  if (++state->chunk_pos < GM2_CHUNK_FRAMES) {
    return 0;
  }
  state->chunk_pos = 0;
  state->freq = gm2_job_smooth(state->freq, state->freq_target, GM2_JOB_SMOOTH_STEP);
  state->wet = gm2_job_smooth(state->wet, wet, GM2_JOB_SMOOTH_STEP);
  state->dry = gm2_job_smooth(state->dry, dry, GM2_JOB_SMOOTH_STEP);
  return 1;
}

void gm2_wah_core_state_init(gm2_wah_core_state *state, int32_t sweep, int32_t wet, int32_t dry) {
  for (unsigned channel = 0; channel < GM2_CHANNELS; channel++) {
    state->svf[channel][0] = 0;
    state->svf[channel][1] = 0;
  }
  state->sweep = sweep;
  state->freq_target = wah_frequency(sweep);
  state->freq = state->freq_target;
  state->wet = wet;
  state->dry = dry;
  state->chunk_pos = 0;
}
