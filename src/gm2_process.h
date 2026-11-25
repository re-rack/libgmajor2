/* What the process code of every effect shares: the block level words, and the
 * four process entry points built on an effect's frame function. For effect and
 * core sources, not for hosts.
 */
#ifndef GM2_PROCESS_H
#define GM2_PROCESS_H

#include <stddef.h>
#include <stdint.h>

#include "gm2_base.h"
#include "gm2_convert.h"

/* The input word every block's level code stages, and the wet word of a full mix at
 * Out Level 0 dB, which the Gate & EQ, the Noise Gate and the Compressor keep,
 * their device blocks having neither knob. A kernel keeps their multiplies: each
 * word sits at or just below 1.0, and its rounding can move a sample by one LSB. */
#define GM2_LEVEL_INPUT 0x7FFFFF
#define GM2_LEVEL_WET 0x7FFFFE

/* Frames the float entry point converts at a time, through the stack. */
#define GM2_PROCESS_F32_FRAMES 256u

/* Defines gm2_<name>_process_split, _f32, _interleaved and _frame. `frame` is
 * the effect's frame function, called as
 *
 *   frame(gm2_<name>_state *state, const gm2_<name>_config *config,
 *         const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS])
 *
 * It always gets an input pair of its own, so it may write `out` before it has
 * read all of `in`. */
#define GM2_PROCESS_DEFINE(name, frame)                                                            \
  void gm2_##name##_process_split(gm2_##name##_state *state, const gm2_##name##_config *config,    \
                                  const int32_t *in_l, const int32_t *in_r, int32_t *out_l,        \
                                  int32_t *out_r, size_t frames) {                                 \
    for (size_t i = 0; i < frames; i++) {                                                          \
      const int32_t in[GM2_CHANNELS] = {in_l[i], in_r[i]};                                         \
      int32_t out[GM2_CHANNELS];                                                                   \
      frame(state, config, in, out);                                                               \
      out_l[i] = out[0];                                                                           \
      out_r[i] = out[1];                                                                           \
    }                                                                                              \
  }                                                                                                \
                                                                                                   \
  void gm2_##name##_process_f32(gm2_##name##_state *state, const gm2_##name##_config *config,      \
                                const float *in_l, const float *in_r, float *out_l, float *out_r,  \
                                size_t frames) {                                                   \
    int32_t left[GM2_PROCESS_F32_FRAMES];                                                          \
    int32_t right[GM2_PROCESS_F32_FRAMES];                                                         \
    for (size_t done = 0; done < frames; done += GM2_PROCESS_F32_FRAMES) {                         \
      size_t count = frames - done;                                                                \
      if (count > GM2_PROCESS_F32_FRAMES) {                                                        \
        count = GM2_PROCESS_F32_FRAMES;                                                            \
      }                                                                                            \
      /* Both inputs convert before either output is written: hosts process in place. */           \
      gm2_f32_to_q23(left, in_l + done, count);                                                    \
      gm2_f32_to_q23(right, in_r + done, count);                                                   \
      gm2_##name##_process_split(state, config, left, right, left, right, count);                  \
      gm2_q23_to_f32(out_l + done, left, count);                                                   \
      gm2_q23_to_f32(out_r + done, right, count);                                                  \
    }                                                                                              \
  }                                                                                                \
                                                                                                   \
  void gm2_##name##_process_interleaved(gm2_##name##_state *state,                                 \
                                        const gm2_##name##_config *config, const int32_t *in,      \
                                        int32_t *out, size_t frames) {                             \
    for (size_t i = 0; i < frames; i++) {                                                          \
      const int32_t pair[GM2_CHANNELS] = {in[2u * i], in[2u * i + 1u]};                            \
      frame(state, config, pair, out + 2u * i);                                                    \
    }                                                                                              \
  }                                                                                                \
                                                                                                   \
  void gm2_##name##_process_frame(gm2_##name##_state *state, const gm2_##name##_config *config,    \
                                  const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {     \
    const int32_t pair[GM2_CHANNELS] = {in[0], in[1]};                                             \
    frame(state, config, pair, out);                                                               \
  }

#endif /* GM2_PROCESS_H */
