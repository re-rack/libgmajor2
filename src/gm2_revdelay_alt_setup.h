/* The de-popped Reverse Delay's setup, which is the delay's own
 * (gm2_revdelay_setup.h). */
#ifndef GM2_REVDELAY_ALT_SETUP_H
#define GM2_REVDELAY_ALT_SETUP_H

#include "gm2_base.h"
#include "gm2_revdelay_alt.h"
#include "gm2_revdelay_setup.h"

typedef gm2_revdelay_setup_raw gm2_revdelay_alt_setup_raw;

int gm2_revdelay_alt_setup_config(const gm2_context *context, const gm2_revdelay_alt_setup_raw *raw,
                                  gm2_revdelay_alt_config *config);

#endif /* GM2_REVDELAY_ALT_SETUP_H */
