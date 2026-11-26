/* The de-popped Reverb's setup, which is the Reverb's own (gm2_reverb_setup.h). */
#ifndef GM2_REVERB_ALT_SETUP_H
#define GM2_REVERB_ALT_SETUP_H

#include "gm2_base.h"
#include "gm2_reverb_alt.h"
#include "gm2_reverb_setup.h"

typedef gm2_reverb_setup_raw gm2_reverb_alt_setup_raw;

int gm2_reverb_alt_setup_config(const gm2_context *context, const gm2_reverb_alt_setup_raw *raw,
                                gm2_reverb_alt_config *config);

#endif /* GM2_REVERB_ALT_SETUP_H */
