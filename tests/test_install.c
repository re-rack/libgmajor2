/* A host built against the installed library alone: gm2.h from the pkg-config
 * include path and libgm2.a. make check-install compiles it as C and as C++.
 */
#include <stdio.h>

#include "gm2.h"

#ifdef __cplusplus
#define TEST_INSTALL_LANGUAGE "C++"
#else
#define TEST_INSTALL_LANGUAGE "C"
#endif

enum { TEST_INSTALL_FRAMES = 512 };

int main(void) {
  static gm2_reverb_setup_raw raw;
  static gm2_reverb_config config;
  static gm2_reverb_state state;
  static float left[TEST_INSTALL_FRAMES];
  static float right[TEST_INSTALL_FRAMES];
  gm2_context context;
  if (gm2_context_init(&context, 48000u) != 0 ||
      gm2_reverb_setup_config(&context, &raw, &config) != 0) {
    printf("install %-7s FAIL  the setup refused\n", TEST_INSTALL_LANGUAGE);
    return 1;
  }
  gm2_reverb_state_init(&state, &config);
  left[0] = 0.5f;
  gm2_reverb_process_f32(&state, &config, left, right, left, right, TEST_INSTALL_FRAMES);
  float energy = 0.0f;
  for (unsigned i = 0; i < TEST_INSTALL_FRAMES; i++) {
    energy += left[i] * left[i] + right[i] * right[i];
  }
  const int ok = energy > 0.0f;
  printf("install %-7s %s  reverb in place, %s conversion\n", TEST_INSTALL_LANGUAGE,
         ok ? "PASS" : "FAIL", gm2_convert_simd());
  return ok ? 0 : 1;
}
