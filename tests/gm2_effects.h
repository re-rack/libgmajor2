/* Every effect the test system covers, in report order: gm2_effect_<name> in
 * tests/effect_<name>.c, gm2_firmware_<name> in a tests/firmware_*.c, and the
 * fixture tests/fixtures/<name>.bin. */
#ifndef GM2_EFFECTS_H
#define GM2_EFFECTS_H

#define GM2_EFFECTS(X)                                                                             \
  X(eq)                                                                                            \
  X(gate)                                                                                          \
  X(wahwah)                                                                                        \
  X(touchwah)                                                                                      \
  X(autowah)                                                                                       \
  X(resfilter)                                                                                     \
  X(touchres)                                                                                      \
  X(tremolo)                                                                                       \
  X(panner)                                                                                        \
  X(comp)                                                                                          \
  X(detune)                                                                                        \
  X(whammy)                                                                                        \
  X(octaver)                                                                                       \
  X(pitch)                                                                                         \
  X(ipitch)                                                                                        \
  X(cchorus)                                                                                       \
  X(achorus)                                                                                       \
  X(trichorus)                                                                                     \
  X(trichorus_asym)                                                                                \
  X(cflanger)                                                                                      \
  X(aflanger)                                                                                      \
  X(tzflanger_neg)                                                                                 \
  X(tzflanger_pos)                                                                                 \
  X(vibrato)                                                                                       \
  X(vintphaser)                                                                                    \
  X(smoothphaser)                                                                                  \
  X(vintunivibe)                                                                                   \
  X(modunivibe)                                                                                    \
  X(pingpong)                                                                                      \
  X(dyndelay)                                                                                      \
  X(dualdelay)                                                                                     \
  X(revdelay)                                                                                      \
  X(reverb_spring)                                                                                 \
  X(reverb_hall)                                                                                   \
  X(reverb_room)                                                                                   \
  X(reverb_plate)

#endif /* GM2_EFFECTS_H */
