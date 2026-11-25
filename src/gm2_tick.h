/* The device's control tick, as the plugins run it, and the words its effects'
 * updaters publish.
 *
 * The firmware moves the Touch Wah, Auto Wah and Touch Res Filter sweeps from its
 * background loop, on a timer tick it takes as 13 ms, not on the audio clock. The
 * plugins run that tick every 13 ms worth of whole four-frame chunks instead, so
 * a given input always gives the same output.
 *
 * The Touch Wah's Response and Sense updaters P:0x0925F3 and P:0x0925A9 are
 * instruction for instruction the Touch Res Filter's P:0x091DC2 and P:0x091D78, so
 * the four words both filters' ticks read come from here.
 */
#ifndef GM2_TICK_H
#define GM2_TICK_H

#include <stdint.h>

#include "gm2_swf.h"

/* The firmware's timer tick in ms: X:0x09871A's 15 over 1.1364, rounded, which
 * is how its updaters come by the number. */
#define GM2_TICK_MS 13

/* `ms` milliseconds in whole chunks, rounded to the nearest and at least one: how
 * the plugins time what the device times off the audio clock. */
unsigned gm2_tick_period(unsigned rate, unsigned ms);

/* The control tick's 13 ms in chunks: 143 at 44100 Hz and 156 at 48000 Hz. */
unsigned gm2_tick_chunks(unsigned rate);

/* P:0x000C78 and P:0x000F3C..0x000F4C, on the first frame of a chunk: a peak hold
 * keeps the larger of itself and the log2 of an envelope, `envelope` being the
 * frame's accumulator before its 24-bit store. */
void gm2_tick_hold(int32_t *peak, int64_t envelope);

/* P:0x09248A and P:0x090494: a tick reads a peak hold, a log2 level, as a level
 * of 0..0x7FFFFF and resets it to 0x800000. */
int32_t gm2_tick_level(int32_t *peak);

/* The touch follower P:0x092541 and P:0x091CF9 step once per tick: the state
 * takes the whole gap to `level` while that gap is at most `threshold`, and
 * `coeff` times the gap above it. */
gm2_swf gm2_tick_follow(gm2_swf state, gm2_swf level, gm2_swf threshold, gm2_swf coeff);

/* P:0x0925F3 and P:0x091DC2: the follower's words for a raw Response value, 0..2
 * for Slow, Medium and Fast. `coeff` is the share of the gap one tick takes and
 * `threshold` the gap it takes it from. */
void gm2_tick_response(int response, gm2_swf *coeff, gm2_swf *threshold);

/* P:0x0925A9 and P:0x091D78: the follower's words for a raw Sense value, 0..10.
 * With s = (10 - Sense) / 11 the level s reads as zero and min(s + 0.6, 1) as full
 * scale, so `span` is their gap's reciprocal and `centre` is -s * span. */
void gm2_tick_sense(int sense, gm2_swf *centre, gm2_swf *span);

#endif /* GM2_TICK_H */
