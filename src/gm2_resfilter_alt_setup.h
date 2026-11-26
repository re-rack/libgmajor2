/* The de-popped Resonance Filter's setup, which is the filter's own
 * (gm2_resfilter_setup.h). */
#ifndef GM2_RESFILTER_ALT_SETUP_H
#define GM2_RESFILTER_ALT_SETUP_H

#include "gm2_base.h"
#include "gm2_resfilter_alt.h"
#include "gm2_resfilter_setup.h"

typedef gm2_resfilter_setup_raw gm2_resfilter_alt_setup_raw;

int gm2_resfilter_alt_setup_config(const gm2_context *context,
                                   const gm2_resfilter_alt_setup_raw *raw,
                                   gm2_resfilter_alt_config *config);

#endif /* GM2_RESFILTER_ALT_SETUP_H */
