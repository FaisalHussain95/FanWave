#include "scan_math.h"

#include <stdio.h>
#include <stdlib.h>

static int failures;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

// Same bands as cc1101_frequency_supported().
static bool supported(uint32_t hz)
{
    return (hz >= 300000000 && hz <= 348000000) || (hz >= 387000000 && hz <= 464000000) ||
           (hz >= 779000000 && hz <= 928000000);
}

static uint32_t hz[FREQ_SCAN_MAX_STEPS];
static int8_t quiet[FREQ_SCAN_MAX_STEPS], held[FREQ_SCAN_MAX_STEPS];

static size_t index_of(size_t n, uint32_t f)
{
    for (size_t i = 0; i < n; i++) {
        if (hz[i] == f) return i;
    }
    return (size_t)-1;
}

int main(void)
{
    // Both low bands: wide enough that the step hits its 250 kHz ceiling.
    uint32_t step = freq_scan_step_hz(300000000, 464000000, supported);
    CHECK(step == 250000);
    size_t n = freq_scan_steps(300000000, 464000000, step, supported, hz, FREQ_SCAN_MAX_STEPS);
    CHECK(n == 193 + 309); // 300–348 and 387–464 inclusive, nothing from the gap
    CHECK(hz[0] == 300000000 && hz[192] == 348000000 && hz[193] == 387000000 && hz[n - 1] == 464000000);

    // A narrow range bottoms out at 50 kHz; a range with no supported part has no steps.
    CHECK(freq_scan_step_hz(433000000, 435000000, supported) == 50000);
    CHECK(freq_scan_steps(350000000, 380000000, 250000, supported, hz, FREQ_SCAN_MAX_STEPS) == 0);
    // The cap is respected.
    CHECK(freq_scan_steps(300000000, 928000000, 25000, supported, hz, 100) == 100);

    // Noise at -100 dBm everywhere, a crystal spur at 390 MHz that is loud in
    // both passes, and a remote at 433.92 MHz that only shows while held.
    n = freq_scan_steps(300000000, 464000000, 250000, supported, hz, FREQ_SCAN_MAX_STEPS);
    for (size_t i = 0; i < n; i++) quiet[i] = held[i] = -100;
    size_t spur = index_of(n, 390000000), remote = index_of(n, 434000000);
    CHECK(spur != (size_t)-1 && remote != (size_t)-1);
    quiet[spur] = held[spur] = -40;
    held[remote] = -45;
    held[remote - 1] = -55; // the receive filter spills into the neighbours
    held[remote + 1] = -60;
    held[index_of(n, 315000000)] = -90; // a small rise elsewhere: a candidate, not the answer

    freq_scan_summary_t s;
    freq_scan_summarise(hz, quiet, held, n, 1000000, &s);
    CHECK(s.floor_dbm == -100);
    CHECK(s.found);
    CHECK(s.peaks[0].hz == 434000000 && s.peaks[0].rise_db == 55 && s.peaks[0].peak_dbm == -45);
    CHECK(s.best_index == remote);
    // The spur never rose, and the remote's neighbours are within the gap, so they don't count again.
    CHECK(s.peak_count == 2 && s.peaks[1].hz == 315000000 && s.peaks[1].rise_db == 10);

    // Nothing pressed: no candidates at all.
    for (size_t i = 0; i < n; i++) held[i] = quiet[i];
    freq_scan_summarise(hz, quiet, held, n, 1000000, &s);
    CHECK(!s.found && s.peak_count == 0);

    // A rise below FREQ_SCAN_FOUND_RISE_DB is reported but not "found".
    held[remote] = quiet[remote] + 7;
    freq_scan_summarise(hz, quiet, held, n, 1000000, &s);
    CHECK(!s.found && s.peak_count == 1 && s.peaks[0].rise_db == 7);

    // Fine sweep: a flat-topped peak centres in its middle, and a lopsided one leans toward the louder side.
    uint32_t fine[9];
    int8_t level[9] = {-90, -80, -50, -48, -48, -48, -50, -80, -90};
    for (int i = 0; i < 9; i++) fine[i] = 433800000 + (uint32_t)i * 25000;
    CHECK(freq_scan_centre_hz(fine, level, 9) == 433900000);
    int8_t lopsided[9] = {-90, -80, -60, -52, -48, -49, -50, -70, -90};
    uint32_t c = freq_scan_centre_hz(fine, lopsided, 9);
    CHECK(c > 433900000 && c < 433925000);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("scan_math: all checks passed\n");
    return EXIT_SUCCESS;
}
