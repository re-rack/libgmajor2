#include "gm2_gate_detector.h"

void gm2_gate_detector_init(gm2_gate_detector *detector) {
  detector->envelope = 0;
  detector->peak = GM2_W24_MIN;
}
