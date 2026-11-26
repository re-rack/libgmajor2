/* The de-popped Intelligent Pitch Shifter's setup, which is the shifter's own
 * (gm2_ipitch_setup.h). */
#ifndef GM2_IPITCH_ALT_SETUP_H
#define GM2_IPITCH_ALT_SETUP_H

#include "gm2_base.h"
#include "gm2_ipitch_alt.h"
#include "gm2_ipitch_setup.h"

typedef gm2_ipitch_setup_raw gm2_ipitch_alt_setup_raw;

int gm2_ipitch_alt_setup_config(const gm2_context *context, const gm2_ipitch_alt_setup_raw *raw,
                                gm2_ipitch_alt_config *config);

#endif /* GM2_IPITCH_ALT_SETUP_H */
