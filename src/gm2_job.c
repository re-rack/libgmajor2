#include "gm2_job.h"

#include "gm2_swf.h"

#include <string.h>

/* X:0x0993B4: the share of the gap a ramp covers per run, and the parabola's
 * middle word, 0.5. */
enum { JOB_RAMP_SCALE = 0x00722D, JOB_FADE_MID = 0x400000 };

int32_t gm2_job_ramp_arm(int32_t live, int32_t target) {
  const gm2_swf gap = gm2_swf_sub(gm2_swf_from_q23(live), gm2_swf_from_q23(target));
  const int32_t step = gm2_swf_to_q23(gm2_swf_mul(gm2_swf_from_q23(JOB_RAMP_SCALE), gap));
  const int32_t size = step < 0 ? -step : step;
  return size < 1 ? 1 : size;
}

int gm2_job_group_run(gm2_job_group *group, int32_t *live, int32_t *from, int32_t *to,
                      const int32_t *staged, unsigned count) {
  const size_t size = sizeof *live * count;
  if (memcmp(to, staged, size) != 0) {
    memcpy(from, live, size);
    memcpy(to, staged, size);
    group->remain = GM2_JOB_GROUP_WHOLE;
  }
  if (group->remain == 0) {
    return 0;
  }
  /* The one-pole job's share per run is half its step word; the share left falls
   * by at least one so that the glide ends. */
  int32_t cut = (int32_t)gm2_asr_n((int64_t)group->remain * GM2_JOB_SMOOTH_STEP, 24);
  group->remain -= cut < 1 ? 1 : cut;
  int moving = 0;
  for (unsigned i = 0; i < count; i++) {
    const int64_t gap = (int64_t)from[i] - staged[i];
    const int32_t left = (int32_t)gm2_asr_n(gap * group->remain + (GM2_JOB_GROUP_WHOLE >> 1), 30);
    live[i] = staged[i] + left;
    moving |= left != 0;
  }
  if (!moving) {
    group->remain = 0;
  }
  return moving;
}

void gm2_job_fade_init(gm2_job_fade *fade, int32_t tap_gain[2]) {
  fade->step = 0;
  fade->pos = GM2_W24_MIN;
  tap_gain[0] = 0;
  tap_gain[1] = GM2_W24_MAX;
}

int gm2_job_fade_check(gm2_job_fade *fade, int32_t tap_offset[2], int32_t offset) {
  if (fade->step != 0) {
    return 1;
  }
  const unsigned silent = gm2_job_fade_silent(fade);
  tap_offset[silent] = offset;
  if (tap_offset[0] == tap_offset[1]) {
    return 0;
  }
  fade->step = silent == 0u ? GM2_JOB_FADE_STEP : -GM2_JOB_FADE_STEP;
  return 1;
}

void gm2_job_fade_step(gm2_job_fade *fade, int32_t tap_gain[2]) {
  const int32_t step = fade->step;
  if (step == 0) {
    /* A settled position moves by zero, which leaves it where it is. */
    return;
  }
  int64_t b = gm2_add24(gm2_word_acc(fade->pos), step);
  if (gm2_es(b)) {
    /* Past a rail the fade is over: the position holds the rail and one tap is
     * left at unity. */
    const int negative = b < 0;
    fade->pos = negative ? GM2_W24_MIN : GM2_W24_MAX;
    fade->step = 0;
    tap_gain[0] = negative ? 0 : GM2_W24_MAX;
    tap_gain[1] = negative ? GM2_W24_MAX : 0;
    return;
  }
  fade->pos = gm2_store24(b);
  int64_t a = gm2_abs56(b);
  const int32_t magnitude = gm2_store24(a);
  b = gm2_add24(b, JOB_FADE_MID);
  a = gm2_mpy(-fade->pos, magnitude);
  a = gm2_addr(b, a);
  const int32_t weight = gm2_store24(a);
  tap_gain[0] = gm2_store24(gm2_mpyr(GM2_W24_MAX, weight));
  tap_gain[1] = gm2_store24(gm2_macr(gm2_word_acc(GM2_W24_MAX), -weight, GM2_W24_MAX));
}
