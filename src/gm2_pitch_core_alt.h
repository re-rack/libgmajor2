/* The de-popped pitch routine: gm2_pitch_core.h with the splice repair of
 * gm2_pitch_core_process_frame_depopped, handed back to the routine's own path
 * once settled (gm2_handback.h).
 *
 * A voice splices when its first tap leaves the delay window, but the firmware
 * splices on the frame's limit flag, which any store of the frame raises. A voice
 * whose crossfade still runs therefore splices anyway, and the cut crossfade
 * clicks: every step of a Dly knob glides the window and pushes the tap out, so a
 * Dly turn clicks on every step. The de-popped path waits the crossfade out; here
 * a turn starts a copy of the routine that splices so, and plays it until the
 * routine itself has settled again.
 */
#ifndef GM2_PITCH_CORE_ALT_H
#define GM2_PITCH_CORE_ALT_H

#include <stdint.h>

#include "gm2_base.h"
#include "gm2_handback.h"
#include "gm2_pitch_core.h"

typedef struct {
  gm2_pitch_core_state exact; /* the routine, always run */
  gm2_pitch_core_state own;   /* the de-popped copy, run from a turn until the hand-back */
  gm2_handback handback;
} gm2_pitch_core_alt_state;

void gm2_pitch_core_alt_state_init(gm2_pitch_core_alt_state *state,
                                   const gm2_pitch_core_config *config);

void gm2_pitch_core_alt_process_frame(gm2_pitch_core_alt_state *state,
                                      const gm2_pitch_core_config *config,
                                      const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]);

#endif /* GM2_PITCH_CORE_ALT_H */
