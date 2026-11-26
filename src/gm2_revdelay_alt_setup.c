#include "gm2_revdelay_alt_setup.h"

int gm2_revdelay_alt_setup_config(const gm2_context *context, const gm2_revdelay_alt_setup_raw *raw,
                                  gm2_revdelay_alt_config *config) {
  return gm2_revdelay_setup_config(context, raw, config);
}
