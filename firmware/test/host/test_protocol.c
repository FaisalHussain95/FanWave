#include "fw_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static uint32_t rng = 12345;
static uint32_t next_rand(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/** Scale every duration by a random factor in [1 - spread, 1 + spread]. */
static void jitter(uint32_t *d, size_t n, float spread)
{
    for (size_t i = 0; i < n; i++) {
        float f = 1.0f + spread * (((float)(next_rand() % 2001) / 1000.0f) - 1.0f);
        d[i] = (uint32_t)(d[i] * f);
    }
}

static void test_pt2262_round_trip(void)
{
    const fw_protocol_t *p = fw_protocol_find("pt2262");
    CHECK(p != NULL);
    uint32_t d[512];
    size_t n = fw_encode(p, 0x3A5C16, 24, 4, 0, d, 512);
    CHECK(n == 4 * 25 * 2);
    jitter(d, n, 0.15f);

    fw_match_t m[8];
    size_t found = fw_decode(d, n, m, 8);
    CHECK(found >= 1);
    CHECK(m[0].protocol == p);
    CHECK(m[0].bits == 0x3A5C16);
    CHECK(m[0].bit_count == 24);
    CHECK(m[0].repeats == 4);
    CHECK(m[0].base_us > 320 && m[0].base_us < 380);
    CHECK(m[0].confidence > 0.9f);
}

static void test_capture_starting_mid_frame(void)
{
    const fw_protocol_t *p = fw_protocol_find("pt2262");
    uint32_t d[512];
    size_t n = fw_encode(p, 0x91F048, 24, 3, 340, d, 512);
    // The receiver woke up partway through the first frame.
    size_t skip = 14;
    fw_match_t m;
    CHECK(fw_decode_with(p, d + skip, n - skip, &m));
    CHECK(m.bits == 0x91F048);
    CHECK(m.repeats == 2);
    CHECK(m.confidence > 0.75f);
}

static void test_inverted_ht12e(void)
{
    const fw_protocol_t *p = fw_protocol_find("ht12e");
    CHECK(p != NULL && p->inverted);
    uint32_t d[256];
    size_t n = fw_encode(p, 0x860, 12, 5, 0, d, 256);
    CHECK(n == 5 * 13 * 2 - 1);
    jitter(d, n, 0.1f);

    fw_match_t m;
    CHECK(fw_decode_with(p, d, n, &m));
    CHECK(m.bits == 0x860);
    CHECK(m.bit_count == 12);
    CHECK(m.repeats >= 4);

    // A long-sync inverted capture must not be read as PT2262.
    CHECK(!fw_decode_with(fw_protocol_find("pt2262"), d, n, &m));
}

static void test_dl_fan_family_ranks_by_base(void)
{
    // A Fanimation-style remote at 346 µs should rank its own row above HT12E (270 µs).
    const fw_protocol_t *p = fw_protocol_find("dl-fanimation");
    uint32_t d[256];
    size_t n = fw_encode(p, 0x37, 12, 4, 0, d, 256);
    fw_match_t m[16];
    size_t found = fw_decode(d, n, m, 16);
    CHECK(found >= 1);
    int seen = 0;
    for (size_t i = 0; i < found; i++) {
        CHECK(m[i].bits == 0x37);
        if (m[i].protocol == p) seen = 1;
    }
    CHECK(seen);
}

static void test_noise_does_not_decode(void)
{
    uint32_t d[400];
    for (size_t i = 0; i < 400; i++) d[i] = 50 + next_rand() % 3000;
    fw_match_t m[8];
    size_t found = fw_decode(d, 400, m, 8);
    for (size_t i = 0; i < found; i++) CHECK(m[i].confidence < 0.6f);
}

static void test_analyze(void)
{
    const fw_protocol_t *p = fw_protocol_find("pt2262");
    uint32_t d[512];
    size_t n = fw_encode(p, 0x3A5C16, 24, 3, 0, d, 512);
    fw_timing_t t = fw_analyze(d, n);
    CHECK(t.short_us == 350);
    CHECK(t.long_us == 1050);
    CHECK(t.gap_us == 31 * 350);
    CHECK(t.pulse_count == 3 * 25);
}

static void test_encode_limits(void)
{
    const fw_protocol_t *p = fw_protocol_find("pt2262");
    uint32_t d[10];
    CHECK(fw_encode(p, 1, 24, 1, 0, d, 10) == 0);
    CHECK(fw_encode(p, 1, 0, 1, 0, d, 10) == 0);
    CHECK(fw_encode(NULL, 1, 4, 1, 0, d, 10) == 0);
    CHECK(fw_protocol_find("nope") == NULL);
    CHECK(fw_protocol_find(NULL) == NULL);
}

static void test_short_input(void)
{
    uint32_t d[3] = {350, 1050, 350};
    fw_match_t m[4];
    CHECK(fw_decode(d, 3, m, 4) == 0);
    CHECK(fw_decode(d, 0, m, 4) == 0);
    fw_timing_t t = fw_analyze(d, 0);
    CHECK(t.pulse_count == 0);
}

static void test_frame_address(void)
{
    const fw_protocol_t *pt = fw_protocol_find("pt2262");
    uint64_t addr, out;
    // The board's remote: address 0x218C0, button code in the low 4 bits.
    CHECK(fw_frame_address(pt, 0x218C03, 24, &addr) && addr == 0x218C0);
    // A copy with another address keeps the button code.
    CHECK(fw_frame_with_address(pt, 0x218C03, 24, 0x5A5A5, &out) && out == 0x5A5A53);
    CHECK(fw_frame_address(pt, out, 24, &addr) && addr == 0x5A5A5);
    // The address must fit in 20 bits, and a frame must be longer than its address.
    CHECK(!fw_frame_with_address(pt, 0x218C03, 24, 0x100000, &out));
    CHECK(!fw_frame_address(pt, 0x218C0, 20, &addr));
    // Protocols without a known address field can't be re-addressed.
    CHECK(!fw_frame_address(fw_protocol_find("rcs2"), 0x123456, 24, &addr));
    // DL fan remotes: a 6-bit DIP address ahead of the command.
    const fw_protocol_t *dl = fw_protocol_find("dl-fanimation");
    CHECK(fw_frame_with_address(dl, 0x5F3, 12, 0x2A, &out) && out == ((0x2AULL << 6) | 0x33));
}

static void test_repeat_train(void)
{
    uint32_t out[64];
    // Ends on a space: repeated as is until it lasts the requested time.
    const uint32_t even[] = {300, 900, 900, 300}; // 2.4 ms
    size_t n = fw_repeat_train(even, 4, 10000, 10000, out, 64);
    CHECK(n == 20); // 5 presses = 12 ms >= 10 ms
    CHECK(out[16] == 300 && out[19] == 300);
    // Ends on a mark: each press gets the gap after it, so marks and spaces keep alternating.
    const uint32_t odd[] = {300, 900, 300};
    n = fw_repeat_train(odd, 3, 8000, 20000, out, 64);
    CHECK(n == 3 * 4); // each press is 9.5 ms with its gap: 3 of them reach 20 ms
    CHECK(n % 4 == 0 && out[3] == 8000 && out[4] == 300);
    // At least one press even for a zero duration; stops when the buffer is full.
    CHECK(fw_repeat_train(even, 4, 0, 0, out, 64) == 4);
    CHECK(fw_repeat_train(even, 4, 0, 1000000, out, 10) == 8);
    CHECK(fw_repeat_train(even, 4, 0, 1000, out, 3) == 0);
}

int main(void)
{
    test_repeat_train();
    test_frame_address();
    test_pt2262_round_trip();
    test_capture_starting_mid_frame();
    test_inverted_ht12e();
    test_dl_fan_family_ranks_by_base();
    test_noise_does_not_decode();
    test_analyze();
    test_encode_limits();
    test_short_input();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("protocol: all checks passed\n");
    return 0;
}
