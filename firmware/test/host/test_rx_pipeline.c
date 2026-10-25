// The receive pipeline end to end, minus the hardware: a held remote button
// arriving between stretches of the noise a CC1101 outputs when nothing is
// transmitting, cut into captures by rf_segment and decoded by fw_decode.

#include <stdio.h>
#include <stdlib.h>

#include "fw_protocol.h"
#include "rf_segment.h"

static int failures;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

static unsigned captures, decoded, wrong;
static const uint64_t CODE = 0x3A5C16;

static void emit(const uint32_t *d, size_t n, void *ctx)
{
    (void)ctx;
    captures++;
    fw_match_t m[4];
    size_t k = fw_decode(d, n, m, 4);
    for (size_t i = 0; i < k; i++) {
        if (m[i].bit_count == 24 && m[i].bits == CODE) {
            decoded++;
            return;
        }
    }
    if (k && m[0].confidence > 0.5f) wrong++;
}

static uint32_t seed = 12345;
static uint32_t rnd(uint32_t lo, uint32_t hi)
{
    seed = seed * 1103515245u + 12345u;
    return lo + (seed >> 8) % (hi - lo + 1);
}

/** Noise as the receiver's slicer produces it: random toggles of 5–400 µs. */
static void noise(rf_segment_t *s, uint32_t total_us, int *level)
{
    for (uint32_t t = 0; t < total_us;) {
        uint32_t d = rnd(5, 400);
        rf_segment_feed(s, d, *level, emit, NULL);
        *level = !*level;
        t += d;
    }
}

int main(void)
{
    static uint32_t buf[4096], frames[4096];
    rf_segment_t s;
    rf_segment_init(&s, buf, 4096, 300000);
    int level = 0;

    const fw_protocol_t *pt = fw_protocol_find("pt2262");
    CHECK(pt != NULL);
    if (!pt) return EXIT_FAILURE;
    // About half a second of a held button: 10 frames, each ending in its sync gap.
    size_t n = fw_encode(pt, CODE, 24, 10, 350, frames, 4096);
    CHECK(n > 400);

    noise(&s, 1000000, &level); // a second of nothing pressed
    unsigned noise_captures = captures;
    CHECK(noise_captures >= 3);  // cut into pieces, never held forever
    CHECK(decoded == 0 && wrong == 0);

    if (level == 1) rf_segment_feed(&s, rnd(5, 400), 1, emit, NULL), level = 0;
    for (size_t i = 0; i < n; i++) rf_segment_feed(&s, frames[i], i % 2 == 0, emit, NULL);
    level = 0;
    noise(&s, 1000000, &level); // released: noise again
    rf_segment_feed(&s, 0, 0, emit, NULL);

    CHECK(decoded >= 1);
    CHECK(wrong == 0);
    printf("rx_pipeline: %u captures, %u decoded the remote\n", captures, decoded);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
