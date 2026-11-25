/* G-Major 2 native effects: the one header a host includes, and the contract
 * every effect follows. From C++, include this header rather than the effect
 * headers it collects.
 *
 * An effect X is declared in gm2_X.h and gm2_X_setup.h and has this interface:
 *
 *   gm2_X_setup_config(context, raw, config)      knob values to the words X reads
 *   gm2_X_state_init(state, config)               the rest state
 *   gm2_X_process_split(state, config, in_l, in_r, out_l, out_r, frames)
 *   gm2_X_process_f32(state, config, in_l, in_r, out_l, out_r, frames)
 *   gm2_X_process_interleaved(state, config, in, out, frames)
 *   gm2_X_process_frame(state, config, in, out)
 *   gm2_X_align(state, config, beats)             effects with an LFO only
 *
 * Setup forces every raw value into its descriptor window, as the firmware's
 * parameter accessor P:0x0832CB does, and returns 0, or -1 for a NULL pointer or
 * a context without a rate. A config is specific to the context's sample rate.
 *
 * The rest state is the one the effect settles in after a long silence: filters
 * and detectors empty, every smoothed word on the config's.
 *
 * A knob change is a new config: the host reruns the setup and passes the result
 * to the next process call, which applies it the way the device applies a knob
 * turn.
 *
 * Samples are Q23, a signed 24-bit value in an int32_t: bits 23..0 are used and
 * higher bits are ignored, the same as reading a word out of DSP memory. The
 * split and f32 entry points take separate left and right buffers, the
 * interleaved one L,R,L,R..., the frame one a single L,R pair; f32 converts with
 * gm2_convert.h. An output may be its own input, as hosts process in place; no
 * other overlap is allowed. Nothing allocates.
 *
 * Align moves the LFO to where it is at song position `beats`, in quarter notes
 * from the song start, when its cycle starts on beat 0; for the host to call at
 * playback start and after a jump. With Tempo at Ignore the LFO is left alone.
 *
 * One context serves all the effects of a plugin instance. The library keeps no
 * global state, so different objects may be used from different threads
 * concurrently, but one object must not be written while another thread uses it.
 * Audio processing never reads the context.
 */
#ifndef GM2_H
#define GM2_H

/* The C library headers come before extern "C" opens. */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "gm2_base.h"
#include "gm2_convert.h"

/* Each setup header includes its effect's header. */
#include "gm2_achorus_setup.h"
#include "gm2_aflanger_setup.h"
#include "gm2_autowah_setup.h"
#include "gm2_cchorus_setup.h"
#include "gm2_cflanger_setup.h"
#include "gm2_comp_setup.h"
#include "gm2_detune_setup.h"
#include "gm2_dualdelay_setup.h"
#include "gm2_dyndelay_setup.h"
#include "gm2_eq_setup.h"
#include "gm2_gate_setup.h"
#include "gm2_ipitch_setup.h"
#include "gm2_modunivibe_setup.h"
#include "gm2_octaver_setup.h"
#include "gm2_panner_setup.h"
#include "gm2_pingpong_setup.h"
#include "gm2_pitch_setup.h"
#include "gm2_resfilter_setup.h"
#include "gm2_revdelay_setup.h"
#include "gm2_reverb_setup.h"
#include "gm2_smoothphaser_setup.h"
#include "gm2_touchres_setup.h"
#include "gm2_touchwah_setup.h"
#include "gm2_tremolo_setup.h"
#include "gm2_trichorus_asym_setup.h"
#include "gm2_trichorus_setup.h"
#include "gm2_tzflanger_neg_setup.h"
#include "gm2_tzflanger_pos_setup.h"
#include "gm2_vibrato_setup.h"
#include "gm2_vintphaser_setup.h"
#include "gm2_vintunivibe_setup.h"
#include "gm2_wahwah_setup.h"
#include "gm2_whammy_setup.h"

#ifdef __cplusplus
}
#endif

#endif /* GM2_H */
