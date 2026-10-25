// Frequency scan: find what a remote transmits on by stepping the CC1101
// across a range and reading RSSI, first with nothing pressed and then while
// the user holds a button. The arithmetic is in components/rf_scan.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#include "scan_math.h"

typedef enum {
    FREQ_SCAN_IDLE,      // never run
    FREQ_SCAN_QUIET,     // measuring the background: don't press anything
    FREQ_SCAN_HOLD,      // hold the remote's button
    FREQ_SCAN_REFINE,    // keep holding: fine steps around the best candidate
    FREQ_SCAN_DONE,
    FREQ_SCAN_FAILED,
    FREQ_SCAN_CANCELLED,
} freq_scan_state_t;

typedef struct {
    uint32_t from_hz, to_hz;
    uint32_t quiet_ms, hold_ms;
} freq_scan_request_t;

typedef struct {
    freq_scan_state_t state;
    float progress; // 0–1 across the whole scan
    freq_scan_request_t request;
    uint32_t step_hz;
    uint32_t candidate_hz; // during FREQ_SCAN_REFINE: where it is narrowing down
    // Once FREQ_SCAN_DONE:
    bool found;
    uint32_t frequency_hz; // refined centre when found, else the best candidate
    int peak_dbm, floor_dbm, rise_db;
    size_t peak_count;
    freq_scan_peak_t peaks[FREQ_SCAN_MAX_PEAKS];
    char error[64]; // FREQ_SCAN_FAILED
} freq_scan_status_t;

/** 300–464 MHz (both low CC1101 bands), 3 s quiet, 9 s holding. */
freq_scan_request_t freq_scan_default_request(void);

/** Start a scan in the background. Fails if one is running or the range has no supported frequencies. */
esp_err_t freq_scan_start(const freq_scan_request_t *request, char err[96]);

/** Stop a running scan early; the receiver goes back as it was. */
void freq_scan_cancel(void);

freq_scan_status_t freq_scan_status(void);
bool freq_scan_finished(freq_scan_state_t state);
const char *freq_scan_state_name(freq_scan_state_t state);

/** Status as JSON (docs/api.md). with_spectrum adds the per-step readings of a finished scan. */
cJSON *freq_scan_json(bool with_spectrum);
