#include "gm2_base.h"

#include <stddef.h>

int gm2_context_init(gm2_context *context, unsigned sample_rate) {
  if (context == NULL) {
    return -1;
  }
  if (sample_rate == 0u || sample_rate > GM2_SAMPLE_RATE_MAX) {
    context->sample_rate = 0u;
    return -1;
  }
  context->sample_rate = sample_rate;
  return 0;
}
