/* The de-popped Pitch Shifter's setup, which is the shifter's own
 * (gm2_pitch_setup.h). */
#ifndef GM2_PITCH_ALT_SETUP_H
#define GM2_PITCH_ALT_SETUP_H

#include "gm2_base.h"
#include "gm2_pitch_alt.h"
#include "gm2_pitch_setup.h"

typedef gm2_pitch_setup_raw gm2_pitch_alt_setup_raw;

int gm2_pitch_alt_setup_config(const gm2_context *context, const gm2_pitch_alt_setup_raw *raw,
                               gm2_pitch_alt_config *config);

#endif /* GM2_PITCH_ALT_SETUP_H */
