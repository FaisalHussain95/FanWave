// Cuts the receiver's continuous level/duration stream into captures (rf.c).
// Plain C with no ESP-IDF dependencies, so test/host builds it too.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t *d;     // durations in µs, alternating mark/space, starting with a mark
    size_t n;
    size_t max;      // capacity of d: a capture is cut here
    uint32_t max_us; // and here, so a held button arrives in pieces
    int level;       // level of d[n - 1], -1 when empty
    uint32_t us;     // total length so far
} rf_segment_t;

/** Called with each finished capture; the durations are only valid during the call. */
typedef void (*rf_segment_emit_fn)(const uint32_t *durations, size_t count, void *ctx);

void rf_segment_init(rf_segment_t *s, uint32_t *buffer, size_t max, uint32_t max_us);

/** Drop the capture in progress without emitting it. */
void rf_segment_reset(rf_segment_t *s);

/**
 * Add one level held for us microseconds. Leading spaces are skipped, runs of
 * the same level merge, and us == 0 (the RMT's end-of-receive marker) ends the
 * capture. Emits whenever a capture ends.
 */
void rf_segment_feed(rf_segment_t *s, uint32_t us, int level, rf_segment_emit_fn emit, void *ctx);

/** End the capture in progress, emitting it unless it's empty. A trailing space is dropped. */
void rf_segment_end(rf_segment_t *s, rf_segment_emit_fn emit, void *ctx);

#ifdef __cplusplus
}
#endif
