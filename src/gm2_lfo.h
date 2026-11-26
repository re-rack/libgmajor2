/* LFO rate setup on the firmware's software float: the Speed and Tempo rates and
 * the phase step every LFO effect uses, the host's beat, and the song-position
 * phase the plugins add.
 *
 * Speed is a log scale of 80 steps a decade, 0..208 for 0.050..19.95 Hz. Tempo
 * 1..16 picks a note value gm2_note_beats[i] / gm2_note_divisor[i] beats long, and
 * the LFO runs one cycle per note. The device holds its tempo as whole
 * milliseconds per beat, the global Tap Time of 100..3000 ms: a whole-millisecond
 * beat runs the firmware's own steps from that integer, any other length runs the
 * same steps on the exact value, which the firmware cannot check.
 */
#ifndef GM2_LFO_H
#define GM2_LFO_H

#include <stdint.h>

#include "gm2_dsp_math.h"
#include "gm2_rate.h"
#include "gm2_swf.h"

#define GM2_LFO_SPEED_MAX 208
#define GM2_LFO_NOTE_MAX 16
#define GM2_LFO_BEAT_MS_MIN 100.0
#define GM2_LFO_BEAT_MS_MAX 3000.0
/* The factory Tap Time: P:0x083A95 writes 0x1F4 to X:0x099A6B. */
#define GM2_LFO_BEAT_MS_DEFAULT 500.0

/* A host's beat length in ms as the value the tempo steps run on: a NaN, which a
 * wrapper passes when the host has no tempo, gives the factory Tap Time, and
 * every other length is forced into GM2_LFO_BEAT_MS_MIN..GM2_LFO_BEAT_MS_MAX. */
gm2_swf gm2_lfo_beat(double beat_ms);

/* The rate in Hz the rate step selects: Speed step 0..GM2_LFO_SPEED_MAX while
 * `note` is 0 (Tempo at Ignore), as P:0x08F0F8 computes it, otherwise the rate of
 * note value 1..GM2_LFO_NOTE_MAX at a beat of beat_ms milliseconds, as P:0x08F30E
 * computes it from the Tap Time, held between the slowest and the fastest Speed. */
gm2_swf gm2_lfo_rate(int speed, unsigned note, double beat_ms);

/* The phase step per frame for a rate in Hz, as P:0x0918CE derives it. */
int32_t gm2_lfo_step(const gm2_rate_words *rate, gm2_swf hz);

/* An LFO's phase one step on, as the wah's P:0x0003C0, the Tremolo/Panner's
 * P:0x000E60 and the chorus routine advance it: less the wrap word once the sum
 * has left the 24-bit range.
 *
 * The two words add as 24-bit fields, so the sum's own 32-bit wrap is the
 * accumulator's A1 field, and the extension test, the wrap word and the limit that
 * follows are steps on that word. */
static inline int32_t gm2_lfo_advance(int32_t phase, int32_t step) {
  const int32_t sum = (int32_t)((uint32_t)phase + (uint32_t)step);
  if ((uint32_t)sum + 0x800000u >= 0x1000000u) {
    return gm2_limit24((int32_t)((uint32_t)sum - (uint32_t)GM2_W24_MAX));
  }
  return gm2_limit24(sum);
}

/* The same as the Phaser and the Univibe's P:0x00109B advance it: back at zero
 * once the sum has left the 24-bit range. */
static inline int32_t gm2_lfo_advance_restart(int32_t phase, int32_t step) {
  const int32_t sum = (int32_t)((uint32_t)phase + (uint32_t)step);
  if ((uint32_t)sum + 0x800000u >= 0x1000000u) {
    return 0;
  }
  return gm2_limit24(sum);
}

/* P:0x08F111 + 0xD0: the raw Speed step that corresponds to `hz`, as the Tempo
 * updaters write back to the Speed slot and as Univibe P:0x09349C checks against
 * 37. */
int32_t gm2_lfo_rate_to_speed(gm2_swf hz);

/* The phase, as a 1.23 fraction of a cycle, of an LFO whose cycle is one note
 * value 1..GM2_LFO_NOTE_MAX long and starts on beat 0, at song position `beats`
 * in quarter notes; a position that is not finite sits on a cycle boundary. The
 * firmware has no song position; this is the plugins' addition. */
int32_t gm2_lfo_phase_at(double beats, unsigned note);

/* An effect's align call: the phase takes the song position's unless `note` is 0,
 * Tempo at Ignore, where the LFO is left alone. */
static inline void gm2_lfo_align(int32_t *phase, unsigned note, double beats) {
  if (note != 0u) {
    *phase = gm2_lfo_phase_at(beats, note);
  }
}

#endif /* GM2_LFO_H */
