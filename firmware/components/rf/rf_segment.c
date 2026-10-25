#include "rf_segment.h"

void rf_segment_init(rf_segment_t *s, uint32_t *buffer, size_t max, uint32_t max_us)
{
    s->d = buffer;
    s->max = max;
    s->max_us = max_us;
    rf_segment_reset(s);
}

void rf_segment_reset(rf_segment_t *s)
{
    s->n = 0;
    s->level = -1;
    s->us = 0;
}

void rf_segment_end(rf_segment_t *s, rf_segment_emit_fn emit, void *ctx)
{
    size_t n = s->n;
    // A trailing space is only the gap before whatever came next.
    if (n && s->level == 0) n--;
    if (n) emit(s->d, n, ctx);
    rf_segment_reset(s);
}

void rf_segment_feed(rf_segment_t *s, uint32_t us, int level, rf_segment_emit_fn emit, void *ctx)
{
    if (us == 0) {
        rf_segment_end(s, emit, ctx);
        return;
    }
    level = level ? 1 : 0;
    if (s->n == 0 && level == 0) return; // wait for the first mark
    if (level == s->level) {
        s->d[s->n - 1] += us;
    } else {
        if (s->n == s->max) {
            rf_segment_end(s, emit, ctx);
            if (level == 0) return;
        }
        s->d[s->n++] = us;
        s->level = level;
    }
    s->us += us;
    if (s->us >= s->max_us) rf_segment_end(s, emit, ctx);
}
