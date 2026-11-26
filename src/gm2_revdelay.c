#include "gm2_revdelay.h"

#include "gm2_process.h"

/* Y+40: the words each tap walks away from the write point per frame, which the
 * write point's own two-word step turns into one frame backwards. */
enum { REVDELAY_STEP = 4 };

/* Where the guarded grain ends: 40,000 frames of walk, which leaves the 2,004-frame
 * crossfade and then some before the 43,200 frames a tap's walk fits. */
enum { REVDELAY_WALK_GUARD = 160000 };

/* The block stages no tap offset of its own: the taps carry 0 until the first
 * splice. */
static const int32_t k_rest_offset[GM2_DELAY_CORE_LINES] = {0, 0};

/* How far the audible tap has walked: the splice resets the silent one, so the
 * walk that can wrap under gain is the other one's. */
static int32_t revdelay_walk(const gm2_delay_core_state *core) {
  const unsigned silent = gm2_job_fade_silent(&core->line[0].fade);
  return core->line[0].rev_offset[1u - silent];
}

/* 1 once the audible tap's walk is within a crossfade of the buffer's end. */
static int revdelay_wrap_near(const gm2_revdelay_state *state) {
  return revdelay_walk(&state->core) >= REVDELAY_WALK_GUARD;
}

/* P:0x094424: the silent tap of each line starts a new grain at the write point,
 * its offset the other one of 1 and 3 than the audible tap's, and the crossfade
 * towards it starts. */
static void revdelay_splice(gm2_delay_core_state *core) {
  for (unsigned n = 0; n < GM2_DELAY_CORE_LINES; n++) {
    gm2_delay_core_line *line = &core->line[n];
    const unsigned silent = gm2_job_fade_silent(&line->fade);
    line->rev_offset[silent] = 0;
    (void)gm2_job_fade_check(&line->fade, line->tap_offset,
                             line->tap_offset[1u - silent] == 1 ? 3 : 1);
  }
}

/* One frame: the routine, then the splice once the grain's period of chunks is
 * over and no crossfade runs. */
void gm2_revdelay_advance(gm2_revdelay_state *state, const gm2_revdelay_config *config,
                          int wrap_guard, const int32_t in[GM2_CHANNELS],
                          int32_t out[GM2_CHANNELS]) {
  /* P:0x094735, which a turn of Delay or Tempo runs, re-anchors the grain's
   * timer: the period starts over. */
  if (config->delay_ms != state->delay_ms) {
    state->delay_ms = config->delay_ms;
    state->tick = 0;
  }
  if (gm2_delay_core_process_frame(&state->core, config->coef, NULL, config->wet, config->dry, in,
                                   out) == 0) {
    return;
  }
  state->tick++;
  if (state->tick < config->grain_chunks && (wrap_guard == 0 || !revdelay_wrap_near(state))) {
    return;
  }
  if (state->core.line[0].fade.step != 0 || state->core.line[1].fade.step != 0) {
    return;
  }
  state->tick = 0;
  revdelay_splice(&state->core);
}

static void revdelay_frame(gm2_revdelay_state *state, const gm2_revdelay_config *config,
                           const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  gm2_revdelay_advance(state, config, 0, in, out);
}

void gm2_revdelay_state_init(gm2_revdelay_state *state, const gm2_revdelay_config *config) {
  gm2_delay_core_state_init(&state->core, config->coef, k_rest_offset, REVDELAY_STEP, config->wet,
                            config->dry);
  state->delay_ms = config->delay_ms;
  state->tick = 0;
}

GM2_PROCESS_DEFINE(revdelay, revdelay_frame)
