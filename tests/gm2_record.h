/* Recorder entry points. Each writes <path>.tmp and renames it on success, and
 * prints one summary line. */
#ifndef GM2_RECORD_H
#define GM2_RECORD_H

#include "gm2_effect.h"

typedef struct gm2_firmware gm2_firmware; /* each adapter defines its own struct */

typedef struct {
  const gm2_effect *effect;
  /* A fresh image at GM2_EFFECT_RATE with the effect selected, every knob at its
   * default, Mix 100 %, Out Level 0 dB and Tap Time GM2_EFFECT_BEAT_MS, at rest as
   * the native open() starts: NULL on failure, after a message on stderr. */
  gm2_firmware *(*open)(void);
  /* The input, wet and dry words the block's own level code staged. */
  int (*levels)(gm2_firmware *firmware, int32_t level[3]);
  /* One knob turn the device's way: the raw value, then the parameter's own
   * descriptor callback, and the publication of any live word the port changes
   * at once rather than through its jobs. A raw word of another knob the
   * callback rewrote is put back without its callback: the knob vector is the
   * host's. 0 on success. */
  int (*set_knob)(gm2_firmware *firmware, unsigned knob, int value);
  /* The effect's setup words, in its setup() order, read where the firmware
   * staged them. */
  int (*words)(gm2_firmware *firmware, int32_t *words);
  /* One stereo frame. After every fourth frame the adapter runs the effect's
   * jobs once, and its tick and other timed work on the schedule AGENTS.md
   * fixes. */
  int (*frame)(gm2_firmware *firmware, int32_t left, int32_t right, int32_t *out_left,
               int32_t *out_right);
  void (*close)(gm2_firmware *firmware);
} gm2_firmware_ops;

int gm2_record_effect(const gm2_firmware_ops *ops, const char *path);
int gm2_record_swf(const char *path);
/* Every lookup table gm2_tables.c keeps, as the firmware holds it. */
int gm2_record_tables(const char *path);

#define GM2_FIRMWARE_DECLARE(name) extern const gm2_firmware_ops gm2_firmware_##name;
GM2_EFFECTS(GM2_FIRMWARE_DECLARE)
#undef GM2_FIRMWARE_DECLARE

#endif /* GM2_RECORD_H */
