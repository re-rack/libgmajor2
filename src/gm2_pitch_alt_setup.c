#include "gm2_pitch_alt_setup.h"

int gm2_pitch_alt_setup_config(const gm2_context *context, const gm2_pitch_alt_setup_raw *raw,
                               gm2_pitch_alt_config *config) {
  return gm2_pitch_setup_config(context, raw, config);
}
