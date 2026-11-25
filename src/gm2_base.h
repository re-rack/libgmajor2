/* What every module shares: the frame constants and the context that carries the
 * sample rate. */
#ifndef GM2_BASE_H
#define GM2_BASE_H

/* A frame is one left and one right sample. */
#define GM2_CHANNELS 2u

/* The firmware's processing unit: its per-chunk work and its parameter jobs run
 * once every four frames. */
#define GM2_CHUNK_FRAMES 4u

/* The 24-bit signed maximum, the largest word P:0x000BFD converts. */
#define GM2_SAMPLE_RATE_MAX 0x7FFFFFu

typedef struct {
  unsigned sample_rate; /* Hz; 0 = no rate */
} gm2_context;

/* Sets the rate in Hz: 0 on success; -1 for a NULL context or a rate outside
 * 1..GM2_SAMPLE_RATE_MAX, the range P:0x000BFD converts and P:0x082DC0 accepts.
 * A rejected rate leaves the context without one. */
int gm2_context_init(gm2_context *context, unsigned sample_rate);

/* The rate every setup reads, or 0 when the context holds no valid one. */
static inline unsigned gm2_context_rate(const gm2_context *context) {
  if (!context || context->sample_rate == 0u || context->sample_rate > GM2_SAMPLE_RATE_MAX) {
    return 0u;
  }
  return context->sample_rate;
}

#endif /* GM2_BASE_H */
