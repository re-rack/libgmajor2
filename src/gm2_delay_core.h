/* The delay routine P:0x0011F9..0x00125D, which the Ping Pong, Dynamic, Dual and
 * Reverse Delays run (Delay block subtypes 0..3).
 *
 * Per stereo frame the routine multiplies both input words by the block's input
 * gain, mixes them and the previous frame's two line outputs through the input
 * and feedback matrices, and writes the two sums to a 172,800-word buffer. Each
 * of the two lines reads the buffer through two taps, which crossfade when the
 * line's offset changes, and runs their blend through a first-order low-pass and
 * high-pass. The output matrix turns the two lines' outputs into left and right,
 * and the Mix knob's wet and dry words blend them with the frame's input. In the
 * Reverse Delay each tap also walks away from the write point by a fixed step per
 * frame, which plays the line backwards.
 *
 * The twenty words below and the Mix pair ride one-pole jobs, and each line's
 * offset moves by a tap crossfade (gm2_job.h).
 *
 * The device keeps two such engines and swaps them on a preset change, so that
 * the old preset's echoes ring out. A plugin has no presets, and one engine gives
 * the routine's output.
 */
#ifndef GM2_DELAY_CORE_H
#define GM2_DELAY_CORE_H

#include <stdint.h>

#include "gm2_base.h"
#include "gm2_job.h"

#define GM2_DELAY_CORE_LINES 2u
#define GM2_DELAY_CORE_TAPS 2u
#define GM2_DELAY_CORE_COEFS 20u
#define GM2_DELAY_CORE_BUFFER_WORDS 0x02A300u /* X:0x0AB000..0x0D52FF: 86,400 stereo frames */

/* The positions in coef[]: Y+0..19 of the engine's record, staged at X+0..19. */
enum {
  GM2_DELAY_CORE_INPUT = 0,    /* the 2x2 input matrix */
  GM2_DELAY_CORE_FEEDBACK = 4, /* the 2x2 feedback matrix */
  GM2_DELAY_CORE_OUTPUT = 8,   /* the 2x2 output matrix */
  GM2_DELAY_CORE_FILTER = 12,  /* each line's low-pass pair, then its high-pass pair */
};

/* One line's words in X+31..50 of the engine's record. */
typedef struct {
  int32_t out;                             /* X+31/32: the filtered output, fed back next frame */
  int32_t tap_offset[GM2_DELAY_CORE_TAPS]; /* X+33/35 and X+37/39: words behind the write point */
  int32_t tap_gain[GM2_DELAY_CORE_TAPS];   /* X+34/36 and X+38/40 */
  int32_t rev_offset[GM2_DELAY_CORE_TAPS]; /* X+41/42 and X+46/47: how far each tap has walked */
  int32_t filter[3]; /* X+43..45 and X+48..50: the blend and the two filters */
  gm2_job_fade fade; /* X:0x098E23 and X:0x098E2E */
} gm2_delay_core_line;

typedef struct {
  /* The buffer, and the word the write index's wrap lands on before it moves
   * back down. */
  int32_t buffer[GM2_DELAY_CORE_BUFFER_WORDS + 1u];
  uint32_t write; /* Y+37, less the buffer's base */
  gm2_delay_core_line line[GM2_DELAY_CORE_LINES];
  int32_t coef[GM2_DELAY_CORE_COEFS]; /* the live words */
  int32_t wet;       /* Y:0x055B: the live pair, Y:0x055C the dry one */
  int32_t dry;
  int32_t rev_step; /* Y+40: 0, or the Reverse Delay's 4 */
  unsigned chunk_pos; /* frame within the chunk */
} gm2_delay_core_state;

/* An empty buffer and silent filters on `coef`, both taps of line n settled on
 * tap_offset[n], every tap walking `rev_step` words a frame, and the Mix pair on
 * `wet` and `dry`. */
void gm2_delay_core_state_init(gm2_delay_core_state *state,
                               const int32_t coef[GM2_DELAY_CORE_COEFS],
                               const int32_t tap_offset[GM2_DELAY_CORE_LINES], int32_t rev_step,
                               int32_t wet, int32_t dry);

/* One stereo frame, then the chunk's job run once its last frame is done: the
 * live words move towards `coef` and the Mix pair towards `wet` and `dry`, the
 * words the level code staged, and each line's taps crossfade towards
 * tap_offset[n], the offset staged at X:0x098E2A and X:0x098E35. With a NULL
 * `tap_offset` the taps stay where the effect put them. Returns 1 when the frame
 * ended a chunk and 0 otherwise, for an effect that acts between chunks. */
int gm2_delay_core_process_frame(gm2_delay_core_state *state,
                                 const int32_t coef[GM2_DELAY_CORE_COEFS],
                                 const int32_t *tap_offset, int32_t wet, int32_t dry,
                                 const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_DELAY_CORE_H */
