#include "rf_segment.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

// Everything emitted, kept for checking.
static uint32_t got[16][64];
static size_t got_n[16], emitted;

static void emit(const uint32_t *d, size_t n, void *ctx)
{
    (void)ctx;
    if (emitted < 16) {
        memcpy(got[emitted], d, (n < 64 ? n : 64) * sizeof(uint32_t));
        got_n[emitted] = n;
    }
    emitted++;
}

static void reset_emitted(void)
{
    emitted = 0;
    memset(got_n, 0, sizeof(got_n));
}

int main(void)
{
    uint32_t buf[8];
    rf_segment_t s;

    // Leading space is skipped, same levels merge, the end marker emits, a trailing space is dropped.
    rf_segment_init(&s, buf, 8, 1000000);
    reset_emitted();
    rf_segment_feed(&s, 500, 0, emit, NULL);  // silence before the first mark
    rf_segment_feed(&s, 300, 1, emit, NULL);
    rf_segment_feed(&s, 50, 1, emit, NULL);   // same level: merges into 350
    rf_segment_feed(&s, 1000, 0, emit, NULL);
    rf_segment_feed(&s, 350, 1, emit, NULL);
    rf_segment_feed(&s, 9000, 0, emit, NULL); // trailing gap
    CHECK(emitted == 0);
    rf_segment_feed(&s, 0, 0, emit, NULL);    // RMT end marker
    CHECK(emitted == 1 && got_n[0] == 3);
    CHECK(got[0][0] == 350 && got[0][1] == 1000 && got[0][2] == 350);
    CHECK(s.n == 0 && s.level == -1);

    // An end marker with nothing collected emits nothing.
    rf_segment_feed(&s, 0, 0, emit, NULL);
    CHECK(emitted == 1);

    // Full buffer: the capture is cut and the next mark starts a new one; a space at the cut is dropped.
    reset_emitted();
    for (int i = 0; i < 8; i++) rf_segment_feed(&s, 100 + i, i % 2 == 0, emit, NULL); // 8 durations, ends on a space
    CHECK(emitted == 0 && s.n == 8);
    rf_segment_feed(&s, 200, 1, emit, NULL); // no room: cut, then this mark starts the next capture
    CHECK(emitted == 1 && got_n[0] == 7);     // trailing space trimmed
    CHECK(s.n == 1 && buf[0] == 200);
    rf_segment_reset(&s);
    for (int i = 0; i < 8; i++) rf_segment_feed(&s, 100, i % 2 == 0, emit, NULL);
    rf_segment_feed(&s, 100, 0, emit, NULL); // same level as the last: merges, no cut needed
    CHECK(emitted == 1 && s.n == 8 && buf[7] == 200);

    // Time limit: a long run of noise is cut into pieces rather than held forever.
    uint32_t big[4096];
    rf_segment_init(&s, big, 4096, 300000);
    reset_emitted();
    for (int i = 0; i < 40000; i++) rf_segment_feed(&s, 20, i % 2 == 0, emit, NULL); // 800 ms of 20 µs toggles
    CHECK(emitted >= 2);
    for (size_t k = 0; k < emitted && k < 16; k++) CHECK(got_n[k] > 0 && got_n[k] <= 4096);

    // Reset drops the capture in progress.
    rf_segment_init(&s, buf, 8, 1000000);
    reset_emitted();
    rf_segment_feed(&s, 300, 1, emit, NULL);
    rf_segment_reset(&s);
    rf_segment_end(&s, emit, NULL);
    CHECK(emitted == 0);

    // Any nonzero level counts as a mark.
    rf_segment_feed(&s, 300, 7, emit, NULL);
    rf_segment_feed(&s, 300, 1, emit, NULL);
    CHECK(s.n == 1 && buf[0] == 600);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("rf_segment: all checks passed\n");
    return EXIT_SUCCESS;
}
