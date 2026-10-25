#include "scan_math.h"

#include <string.h>

#define FINE_HZ 25000
#define MIN_STEP_HZ 50000
#define MAX_STEP_HZ 250000
#define TARGET_STEPS 400

uint32_t freq_scan_step_hz(uint32_t from_hz, uint32_t to_hz, freq_scan_supported_fn supported)
{
    // Measure the supported span in 1 MHz pieces; the bands start and end on whole MHz.
    uint64_t span = 0;
    for (uint64_t f = from_hz; f < to_hz; f += 1000000) {
        uint64_t end = f + 1000000 < to_hz ? f + 1000000 : to_hz;
        if (supported((uint32_t)f) && supported((uint32_t)end)) span += end - f;
    }
    uint64_t step = (span / TARGET_STEPS + FINE_HZ - 1) / FINE_HZ * FINE_HZ;
    if (step < MIN_STEP_HZ) step = MIN_STEP_HZ;
    if (step > MAX_STEP_HZ) step = MAX_STEP_HZ;
    return (uint32_t)step;
}

size_t freq_scan_steps(uint32_t from_hz, uint32_t to_hz, uint32_t step_hz, freq_scan_supported_fn supported, uint32_t *out,
                  size_t max)
{
    size_t n = 0;
    if (!step_hz) return 0;
    for (uint64_t f = from_hz; f <= to_hz && n < max; f += step_hz) {
        if (supported((uint32_t)f)) out[n++] = (uint32_t)f;
    }
    return n;
}

static int median(const int8_t *v, size_t n)
{
    if (!n) return -128;
    size_t count[256] = {0};
    for (size_t i = 0; i < n; i++) count[v[i] + 128]++;
    size_t seen = 0;
    for (int b = 0; b < 256; b++) {
        seen += count[b];
        if (seen * 2 >= n) return b - 128;
    }
    return 127;
}

static uint32_t distance(uint32_t a, uint32_t b)
{
    return a > b ? a - b : b - a;
}

void freq_scan_summarise(const uint32_t *hz, const int8_t *quiet, const int8_t *held, size_t n, uint32_t min_gap_hz,
                    freq_scan_summary_t *out)
{
    memset(out, 0, sizeof(*out));
    out->floor_dbm = median(quiet, n);
    while (out->peak_count < FREQ_SCAN_MAX_PEAKS) {
        int best = -1, best_rise = FREQ_SCAN_CANDIDATE_RISE_DB - 1;
        for (size_t i = 0; i < n; i++) {
            int rise = held[i] - quiet[i];
            if (rise <= best_rise) continue;
            bool near_taken = false;
            for (size_t p = 0; p < out->peak_count; p++) {
                if (distance(hz[i], out->peaks[p].hz) < min_gap_hz) near_taken = true;
            }
            if (near_taken) continue;
            best = (int)i;
            best_rise = rise;
        }
        if (best < 0) break;
        if (out->peak_count == 0) out->best_index = (size_t)best;
        out->peaks[out->peak_count++] = (freq_scan_peak_t){.hz = hz[best], .peak_dbm = held[best], .rise_db = best_rise};
    }
    out->found = out->peak_count > 0 && out->peaks[0].rise_db >= FREQ_SCAN_FOUND_RISE_DB;
}

uint32_t freq_scan_centre_hz(const uint32_t *hz, const int8_t *level, size_t n)
{
    if (!n) return 0;
    int top = -128;
    for (size_t i = 0; i < n; i++) {
        if (level[i] > top) top = level[i];
    }
    // Weight each step near the top by how close it is, so one loud edge step can't drag the centre.
    uint64_t sum = 0, weight = 0;
    for (size_t i = 0; i < n; i++) {
        int w = level[i] - (top - 3) + 1;
        if (w <= 0) continue;
        sum += (uint64_t)hz[i] * (uint64_t)w;
        weight += (uint64_t)w;
    }
    return (uint32_t)(sum / weight);
}
