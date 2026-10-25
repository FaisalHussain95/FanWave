// The arithmetic behind the frequency scan (main/freq_scan.c): which frequencies to
// visit, and which one a remote was transmitting on. No hardware here.
//
// The scan measures twice. A quiet pass records the strongest RSSI at each
// step with nothing pressed; a second pass does the same while the user holds
// a button. The answer is the step whose level rose most over its own quiet
// reading, not simply the loudest one: the CC1101 has spurious responses at
// multiples of its 26 MHz crystal (390, 416, 442 MHz…) and the hub's own
// electronics radiate too, and both read loud whether or not a remote is on.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FREQ_SCAN_MAX_STEPS 1200
#define FREQ_SCAN_MAX_PEAKS 3
/** A rise this large over the quiet pass counts as a remote. */
#define FREQ_SCAN_FOUND_RISE_DB 10
/** Smaller rises are left out of the candidate list. */
#define FREQ_SCAN_CANDIDATE_RISE_DB 6

typedef bool (*freq_scan_supported_fn)(uint32_t hz);

/**
 * Step size for a sweep from from_hz to to_hz: about 400 steps across the
 * supported part of the range, rounded up to 25 kHz and kept between 50 and
 * 250 kHz, which stays inside the 325 kHz receive bandwidth.
 */
uint32_t freq_scan_step_hz(uint32_t from_hz, uint32_t to_hz, freq_scan_supported_fn supported);

/** Supported frequencies from from_hz to to_hz in step_hz steps. Returns how many were written. */
size_t freq_scan_steps(uint32_t from_hz, uint32_t to_hz, uint32_t step_hz, freq_scan_supported_fn supported, uint32_t *out,
                  size_t max);

typedef struct {
    uint32_t hz;
    int peak_dbm; // strongest reading while the button was held
    int rise_db;  // how far that is above the same step's quiet reading
} freq_scan_peak_t;

typedef struct {
    int floor_dbm;                    // median quiet reading: the band's noise floor
    bool found;                       // peaks[0] rose at least FREQ_SCAN_FOUND_RISE_DB
    size_t peak_count;
    freq_scan_peak_t peaks[FREQ_SCAN_MAX_PEAKS]; // largest rise first, at least min_gap_hz apart
    size_t best_index;                // step index of peaks[0]
} freq_scan_summary_t;

/** Compare the two passes. quiet and held hold one reading per step of hz[]. */
void freq_scan_summarise(const uint32_t *hz, const int8_t *quiet, const int8_t *held, size_t n, uint32_t min_gap_hz,
                    freq_scan_summary_t *out);

/**
 * Centre of a peak from a fine sweep: the weighted middle of the steps within
 * 3 dB of the strongest, which is steadier than taking the single loudest step
 * when the receive filter is wider than the step.
 */
uint32_t freq_scan_centre_hz(const uint32_t *hz, const int8_t *level, size_t n);

#ifdef __cplusplus
}
#endif
