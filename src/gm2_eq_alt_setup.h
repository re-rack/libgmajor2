/* The de-popped EQ's setup, which is the EQ's own (gm2_eq_setup.h). */
#ifndef GM2_EQ_ALT_SETUP_H
#define GM2_EQ_ALT_SETUP_H

#include "gm2_base.h"
#include "gm2_eq_alt.h"
#include "gm2_eq_setup.h"

typedef gm2_eq_setup_raw gm2_eq_alt_setup_raw;

int gm2_eq_alt_setup_config(const gm2_context *context, const gm2_eq_alt_setup_raw *raw,
                            gm2_eq_alt_config *config);

#endif /* GM2_EQ_ALT_SETUP_H */
