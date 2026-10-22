/* V60 input conversion and key debounce, shared by the XNU port and host tests. */
#ifndef V60_INPUT_POLICY_H
#define V60_INPUT_POLICY_H
#include <stdint.h>
static inline uint32_t v60_normalize(int pixel, unsigned extent)
{
    if (pixel <= 0) return 0;
    if ((unsigned)pixel >= extent - 1) return 65535;
    return (uint32_t)((uint64_t)(unsigned)pixel * 65535 / (extent - 1));
}
struct v60_key_state { unsigned candidate, count, down, armed; };
/* -1: no transition; 0/1: debounced release/press. Require release before arming. */
static inline int v60_key_sample(struct v60_key_state *s, int raw)
{
    if (raw < 0) return -1;
    if (!s->armed) {
        if (raw) return -1;
        s->armed = 1;
    }
    if ((unsigned)raw != s->candidate) { s->candidate = (unsigned)raw; s->count = 1; }
    else if (s->count < 2) s->count++;
    if (s->count < 2 || s->down == (unsigned)raw) return -1;
    s->down = (unsigned)raw;
    return raw;
}
#endif
