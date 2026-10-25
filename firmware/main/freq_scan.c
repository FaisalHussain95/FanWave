// Frequency scan. See freq_scan.h for the idea and components/rf_scan for the
// arithmetic; this file drives the radio and reports progress.
//
// Each step retunes the CC1101 (cc1101_tune_rx, which recalibrates on the way
// into RX), waits for RX, then keeps the loudest RSSI reading over DWELL_US. A
// remote keys its carrier on and off, so a short dwell can land in a gap;
// sweeping repeatedly while the button is held and keeping each step's
// loudest reading across sweeps fills those in. Steps sleep through the
// calibration rather than spinning, so a scan never starves other tasks.

#include "freq_scan.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "cc1101.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hub.h"

static const char *TAG = "scan";

#define DWELL_US 1500
#define SAMPLE_GAP_US 30
#define RX_TIMEOUT_US 3000
#define SETTLE_US 100       // let the RSSI average settle after entering RX
#define FINE_STEP_HZ 25000
#define MAX_FINE 24
#define MIN_GAP_HZ 1000000  // candidates closer than this are the same signal
#define SPECTRUM_POINTS 240
#define PROGRESS_MS 400

static SemaphoreHandle_t s_lock; // s_status and the step arrays
static freq_scan_status_t s_status;
static volatile bool s_cancel;
static uint32_t *s_hz;
static int8_t *s_quiet, *s_held;
static size_t s_n;
static int64_t s_started_us, s_last_report_us;

freq_scan_request_t freq_scan_default_request(void)
{
    return (freq_scan_request_t){.from_hz = 300000000, .to_hz = 464000000, .quiet_ms = 3000, .hold_ms = 9000};
}

bool freq_scan_finished(freq_scan_state_t state)
{
    return state == FREQ_SCAN_IDLE || state == FREQ_SCAN_DONE || state == FREQ_SCAN_FAILED || state == FREQ_SCAN_CANCELLED;
}

const char *freq_scan_state_name(freq_scan_state_t state)
{
    static const char *names[] = {"idle", "quiet", "hold", "refine", "done", "failed", "cancelled"};
    return state < sizeof(names) / sizeof(names[0]) ? names[state] : "idle";
}

freq_scan_status_t freq_scan_status(void)
{
    if (!s_lock) return (freq_scan_status_t){.state = FREQ_SCAN_IDLE};
    xSemaphoreTake(s_lock, portMAX_DELAY);
    freq_scan_status_t s = s_status;
    xSemaphoreGive(s_lock);
    return s;
}

static double mhz(uint32_t hz)
{
    return round(hz / 1e3) / 1e3; // to the kHz
}

cJSON *freq_scan_json(bool with_spectrum)
{
    freq_scan_status_t s = freq_scan_status();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "state", freq_scan_state_name(s.state));
    if (s.state == FREQ_SCAN_IDLE) return o;
    cJSON_AddNumberToObject(o, "progress", round(s.progress * 100) / 100);
    cJSON_AddNumberToObject(o, "fromMHz", mhz(s.request.from_hz));
    cJSON_AddNumberToObject(o, "toMHz", mhz(s.request.to_hz));
    cJSON_AddNumberToObject(o, "stepKHz", s.step_hz / 1000);
    if (s.state == FREQ_SCAN_REFINE) cJSON_AddNumberToObject(o, "candidateMHz", mhz(s.candidate_hz));
    if (s.state == FREQ_SCAN_FAILED) cJSON_AddStringToObject(o, "error", s.error);
    if (s.state != FREQ_SCAN_DONE) return o;

    cJSON_AddBoolToObject(o, "found", s.found);
    if (s.peak_count) {
        cJSON_AddNumberToObject(o, "frequencyMHz", mhz(s.frequency_hz));
        cJSON_AddNumberToObject(o, "peakDbm", s.peak_dbm);
        cJSON_AddNumberToObject(o, "riseDb", s.rise_db);
    }
    cJSON_AddNumberToObject(o, "floorDbm", s.floor_dbm);
    cJSON *cands = cJSON_AddArrayToObject(o, "candidates");
    for (size_t i = 0; i < s.peak_count; i++) {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddNumberToObject(c, "frequencyMHz", mhz(s.peaks[i].hz));
        cJSON_AddNumberToObject(c, "peakDbm", s.peaks[i].peak_dbm);
        cJSON_AddNumberToObject(c, "riseDb", s.peaks[i].rise_db);
        cJSON_AddItemToArray(cands, c);
    }
    if (!with_spectrum) return o;

    // [MHz, quiet dBm, held dBm], merged into at most SPECTRUM_POINTS bins by taking each bin's loudest.
    cJSON *spec = cJSON_AddArrayToObject(o, "spectrum");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t per = (s_n + SPECTRUM_POINTS - 1) / SPECTRUM_POINTS;
    for (size_t start = 0; per && start < s_n; start += per) {
        size_t end = start + per < s_n ? start + per : s_n, loud = start;
        int quiet = INT8_MIN;
        for (size_t i = start; i < end; i++) {
            if (s_held[i] > s_held[loud]) loud = i;
            if (s_quiet[i] > quiet) quiet = s_quiet[i];
        }
        cJSON *p = cJSON_CreateArray();
        cJSON_AddItemToArray(p, cJSON_CreateNumber(mhz(s_hz[loud])));
        cJSON_AddItemToArray(p, cJSON_CreateNumber(quiet));
        cJSON_AddItemToArray(p, cJSON_CreateNumber(s_held[loud]));
        cJSON_AddItemToArray(spec, p);
    }
    xSemaphoreGive(s_lock);
    return o;
}

static void broadcast(bool final)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "scan");
    cJSON_AddItemToObject(o, "scan", freq_scan_json(final));
    api_broadcast(o);
    s_last_report_us = esp_timer_get_time();
}

static void set_state(freq_scan_state_t state)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = state;
    xSemaphoreGive(s_lock);
    broadcast(freq_scan_finished(state));
}

/** Update progress, and tell the web app every PROGRESS_MS. */
static void tick(void)
{
    int64_t now = esp_timer_get_time();
    float total = (float)(s_status.request.quiet_ms + s_status.request.hold_ms) * 1000;
    float p = (now - s_started_us) / total;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.progress = p < 0.99f ? p : 0.99f;
    xSemaphoreGive(s_lock);
    if (now - s_last_report_us >= PROGRESS_MS * 1000LL) broadcast(false);
}

static int measure(uint32_t hz)
{
    if (cc1101_tune_rx(hz) != ESP_OK) return INT8_MIN;
    vTaskDelay(1); // most of the ~800 µs calibration
    int64_t deadline = esp_timer_get_time() + RX_TIMEOUT_US;
    while (!cc1101_in_rx()) {
        if (esp_timer_get_time() > deadline) return INT8_MIN;
        esp_rom_delay_us(50);
    }
    esp_rom_delay_us(SETTLE_US);
    int loudest = INT8_MIN;
    int64_t end = esp_timer_get_time() + DWELL_US;
    while (esp_timer_get_time() < end) {
        int v = cc1101_rssi_dbm();
        if (v > loudest) loudest = v;
        esp_rom_delay_us(SAMPLE_GAP_US);
    }
    return loudest > INT8_MAX ? INT8_MAX : loudest;
}

/**
 * Sweep hz[] repeatedly, keeping each step's loudest reading in level[],
 * until at least one full pass is done and until_us has passed. False if cancelled.
 */
static bool sweep(const uint32_t *hz, int8_t *level, size_t n, int64_t until_us)
{
    for (size_t i = 0; i < n; i++) level[i] = INT8_MIN;
    do {
        for (size_t i = 0; i < n; i++) {
            if (s_cancel) return false;
            int v = measure(hz[i]);
            if (v > level[i]) level[i] = (int8_t)v;
            tick();
        }
    } while (esp_timer_get_time() < until_us);
    return true;
}

static void finish(freq_scan_state_t state)
{
    hub_scan_end();
    if (state == FREQ_SCAN_DONE) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.progress = 1;
        xSemaphoreGive(s_lock);
    }
    set_state(state);
}

static void freq_scan_task(void *arg)
{
    const freq_scan_request_t req = s_status.request;
    int64_t t0 = s_started_us;
    ESP_LOGI(TAG, "%.3f-%.3f MHz in %lu kHz steps (%u of them)", req.from_hz / 1e6, req.to_hz / 1e6,
             (unsigned long)(s_status.step_hz / 1000), (unsigned)s_n);

    if (!sweep(s_hz, s_quiet, s_n, t0 + req.quiet_ms * 1000LL)) goto cancelled;

    set_state(FREQ_SCAN_HOLD);
    int64_t hold_start = esp_timer_get_time();
    // Coarse steps for the first three quarters of the hold, fine steps after.
    if (!sweep(s_hz, s_held, s_n, hold_start + req.hold_ms * 750LL)) goto cancelled;

    freq_scan_summary_t sum;
    freq_scan_summarise(s_hz, s_quiet, s_held, s_n, MIN_GAP_HZ, &sum);
    if (sum.floor_dbm == INT8_MIN) {
        // measure() returns INT8_MIN when the chip doesn't retune or reach RX.
        ESP_LOGE(TAG, "the CC1101 stopped answering during the scan");
        xSemaphoreTake(s_lock, portMAX_DELAY);
        snprintf(s_status.error, sizeof(s_status.error), "The radio stopped answering.");
        xSemaphoreGive(s_lock);
        finish(FREQ_SCAN_FAILED);
        vTaskDelete(NULL);
        return;
    }
    uint32_t frequency = sum.peak_count ? sum.peaks[0].hz : 0;
    int peak = sum.peak_count ? sum.peaks[0].peak_dbm : INT8_MIN;

    if (sum.found && s_status.step_hz > FINE_STEP_HZ) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.candidate_hz = sum.peaks[0].hz;
        xSemaphoreGive(s_lock);
        set_state(FREQ_SCAN_REFINE);
        uint32_t fine_hz[MAX_FINE];
        int8_t fine[MAX_FINE];
        uint32_t c = sum.peaks[0].hz, w = s_status.step_hz;
        size_t nf = freq_scan_steps(c - w, c + w, FINE_STEP_HZ, cc1101_frequency_supported, fine_hz, MAX_FINE);
        if (!sweep(fine_hz, fine, nf, hold_start + req.hold_ms * 1000LL)) goto cancelled;
        frequency = freq_scan_centre_hz(fine_hz, fine, nf);
        for (size_t i = 0; i < nf; i++) {
            if (fine[i] > peak) peak = fine[i];
        }
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.found = sum.found;
    s_status.frequency_hz = frequency;
    s_status.peak_dbm = peak;
    s_status.floor_dbm = sum.floor_dbm;
    s_status.rise_db = sum.peak_count ? peak - s_quiet[sum.best_index] : 0;
    s_status.peak_count = sum.peak_count;
    memcpy(s_status.peaks, sum.peaks, sizeof(sum.peaks));
    xSemaphoreGive(s_lock);

    if (sum.found) {
        ESP_LOGI(TAG, "found %.3f MHz: %d dBm, %d dB over the background (floor %d dBm)", frequency / 1e6, peak,
                 s_status.rise_db, sum.floor_dbm);
    } else {
        ESP_LOGI(TAG, "nothing rose %d dB over the background (floor %d dBm)", FREQ_SCAN_FOUND_RISE_DB, sum.floor_dbm);
    }
    for (size_t i = 0; i < sum.peak_count; i++) {
        ESP_LOGI(TAG, "  candidate %.3f MHz: %d dBm, +%d dB", sum.peaks[i].hz / 1e6, sum.peaks[i].peak_dbm,
                 sum.peaks[i].rise_db);
    }
    finish(FREQ_SCAN_DONE);
    vTaskDelete(NULL);
    return;

cancelled:
    ESP_LOGI(TAG, "cancelled");
    finish(FREQ_SCAN_CANCELLED);
    vTaskDelete(NULL);
}

esp_err_t freq_scan_start(const freq_scan_request_t *request, char err[96])
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        s_hz = malloc(FREQ_SCAN_MAX_STEPS * sizeof(uint32_t));
        s_quiet = malloc(FREQ_SCAN_MAX_STEPS);
        s_held = malloc(FREQ_SCAN_MAX_STEPS);
    }
    if (!s_lock || !s_hz || !s_quiet || !s_held) {
        snprintf(err, 96, "Out of memory.");
        return ESP_ERR_NO_MEM;
    }
    freq_scan_request_t req = *request;
    if (req.from_hz >= req.to_hz) {
        snprintf(err, 96, "The range must go from a lower to a higher frequency.");
        return ESP_ERR_INVALID_ARG;
    }
    if (req.quiet_ms < 1000) req.quiet_ms = 1000;
    if (req.quiet_ms > 10000) req.quiet_ms = 10000;
    if (req.hold_ms < 3000) req.hold_ms = 3000;
    if (req.hold_ms > 30000) req.hold_ms = 30000;
    if (!freq_scan_finished(freq_scan_status().state)) {
        snprintf(err, 96, "A scan is already running.");
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t step = freq_scan_step_hz(req.from_hz, req.to_hz, cc1101_frequency_supported);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_n = freq_scan_steps(req.from_hz, req.to_hz, step, cc1101_frequency_supported, s_hz, FREQ_SCAN_MAX_STEPS);
    size_t n = s_n;
    xSemaphoreGive(s_lock);
    if (!n) {
        snprintf(err, 96, "The CC1101 covers 300–348, 387–464 and 779–928 MHz; that range has none of them.");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t e = hub_scan_begin();
    if (e != ESP_OK) {
        snprintf(err, 96, hub_radio_ok() ? "The radio is busy." : "The radio isn't working.");
        return e;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status = (freq_scan_status_t){.state = FREQ_SCAN_QUIET, .request = req, .step_hz = step};
    xSemaphoreGive(s_lock);
    s_cancel = false;
    s_started_us = esp_timer_get_time();
    if (xTaskCreate(freq_scan_task, "scan", 4096, NULL, 4, NULL) != pdPASS) {
        hub_scan_end();
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.state = FREQ_SCAN_FAILED;
        snprintf(s_status.error, sizeof(s_status.error), "Couldn't start the scan task.");
        xSemaphoreGive(s_lock);
        snprintf(err, 96, "Out of memory.");
        return ESP_ERR_NO_MEM;
    }
    broadcast(false);
    return ESP_OK;
}

void freq_scan_cancel(void)
{
    s_cancel = true;
}
