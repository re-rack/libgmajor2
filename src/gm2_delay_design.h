/* The setup the four delays share: the Delay block's updaters for subtypes 0..3.
 *
 *   P:0x094574  Feedback: the input and feedback matrices
 *   P:0x094643  Width, Pan: the output matrix
 *   P:0x094835  Feedback Hi-Cut, Feedback Lo-Cut: the lines' filters
 *   P:0x094735  Delay, Tempo: a line's delay time
 *   P:0x09479E  a delay time's tap offset
 */
#ifndef GM2_DELAY_DESIGN_H
#define GM2_DELAY_DESIGN_H

#include <stdint.h>

#include "gm2_delay_core.h"
#include "gm2_rate.h"

/* The Delay block's subtype numbers. */
typedef enum {
  GM2_DELAY_DESIGN_PINGPONG = 0,
  GM2_DELAY_DESIGN_DYNDELAY = 1,
  GM2_DELAY_DESIGN_DUALDELAY = 2,
  GM2_DELAY_DESIGN_REVDELAY = 3,
} gm2_delay_design_subtype;

/* X+51 at Mix 100 % and Out Level 0 dB: the scale of the output matrix's words. */
#define GM2_DELAY_DESIGN_WET_SCALE 0x7FFFFE

/* P:0x094574: the eight words of the input and feedback matrices, from the raw
 * Feedback values, 0..100 %. Only the Dual Delay has a second one. */
void gm2_delay_design_matrix(gm2_delay_design_subtype subtype, int feedback_1, int feedback_2,
                             int32_t coef[8]);

/* P:0x094643: the four words of the output matrix, from the Ping Pong's raw
 * Width, 0..100 %, or the Dual Delay's raw Pans, -50..50. The Dynamic Delay's are
 * its ducking tick's and stay zero here; the Reverse Delay's are those of two
 * centred pans. */
void gm2_delay_design_output(gm2_delay_design_subtype subtype, int width, int pan_1, int pan_2,
                             int32_t coef[4]);

/* P:0x094835: the eight filter words, from the raw Feedback Hi-Cut, 40..60, and
 * Feedback Lo-Cut, 0..40. */
void gm2_delay_design_filters(const gm2_rate_words *rate, gm2_delay_design_subtype subtype,
                              int fb_hi_cut, int fb_lo_cut, int32_t coef[8]);

/* P:0x094735: a line's delay time in ms. With the raw Tempo at 0 (Ignore) it is
 * the raw Delay, forced into delay_min..delay_max; otherwise the note value 1..16
 * at the host's beat, at most 1800 ms. */
int32_t gm2_delay_design_ms(int delay, int delay_min, int delay_max, int tempo, double beat_ms);

/* P:0x09479E: the tap offset of line 0 or 1 for a delay time of 0..1800 ms. */
int32_t gm2_delay_design_tap_offset(const gm2_rate_words *rate, int32_t delay_ms, unsigned line);

#endif /* GM2_DELAY_DESIGN_H */
