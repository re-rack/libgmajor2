#include "gm2_autowah_setup.h"

#include "gm2_dsp_math.h"
#include "gm2_lfo.h"
#include "gm2_rate.h"
#include "gm2_swf.h"
#include "gm2_tick.h"
#include "gm2_wah_design.h"

/* The descriptor windows P:0x0832CB clamps the raw values into. */
enum {
  SPEED_MIN = 0,
  SPEED_MAX = GM2_LFO_SPEED_MAX,
  TEMPO_MIN = 0,
  TEMPO_MAX = GM2_LFO_NOTE_MAX,
  SHAPE_MIN = 0,
  SHAPE_MAX = 1,
};

int gm2_autowah_setup_config(const gm2_context *context, const gm2_autowah_setup_raw *raw,
                             gm2_autowah_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }
  const int speed = gm2_clamp_raw(raw->speed, SPEED_MIN, SPEED_MAX);
  const int tempo = gm2_clamp_raw(raw->tempo, TEMPO_MIN, TEMPO_MAX);
  const int shape = gm2_clamp_raw(raw->shape, SHAPE_MIN, SHAPE_MAX);

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);

  config->shift = gm2_wah_design_shift(raw->range);
  config->shape = shape;
  /* P:0x0924C1: the LFO advances once a chunk, so its step is four frames of
   * phase. */
  const gm2_swf hz = gm2_lfo_rate(speed, (unsigned)tempo, raw->beat_ms);
  config->lfo_step = gm2_swf_to_q23(gm2_swf_mul(gm2_swf_mul(hz, gm2_swf_four), rate_words.period));
  config->note = (unsigned)tempo;
  config->tick_chunks = gm2_tick_chunks(rate);
  return 0;
}
