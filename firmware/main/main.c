// FanWave firmware: radio, Wi-Fi, the HTTP/WebSocket API the web app uses,
// and a serial console for bring-up.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "captures.h"
#include "cc1101.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fans.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "fw_protocol.h"
#include "hub.h"
#include "nvs_flash.h"
#include "rf.h"
#include "sdkconfig.h"
#include "settings.h"
#include "status_led.h"
#include "storage.h"
#include "wifi.h"

static const char *TAG = "fanwave";

static SemaphoreHandle_t s_lock;    // s_radio and s_last
static SemaphoreHandle_t s_tx_lock; // one transmission (and its radio switch) at a time
static cc1101_config_t s_radio;
static bool s_radio_ok;
static uint32_t *s_last;
static size_t s_last_count;
static volatile int s_min_rssi = CONFIG_FANWAVE_RF_MIN_RSSI;
static volatile bool s_scanning; // written with s_tx_lock held
static bool s_scan_was_listening;

esp_err_t hub_set_radio(const cc1101_config_t *config)
{
    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    if (s_scanning) {
        xSemaphoreGive(s_tx_lock);
        return ESP_ERR_INVALID_STATE;
    }
    bool listening = radio_listening();
    esp_err_t err = radio_set_listening(false);
    if (err == ESP_OK) err = cc1101_configure(config);
    if (err == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_radio = *config;
        xSemaphoreGive(s_lock);
    }
    if (listening) {
        esp_err_t rx = radio_set_listening(true);
        if (err == ESP_OK) err = rx;
    }
    xSemaphoreGive(s_tx_lock);
    return err;
}

esp_err_t hub_set_listening(bool on)
{
    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    esp_err_t err = s_scanning ? ESP_ERR_INVALID_STATE : radio_set_listening(on);
    xSemaphoreGive(s_tx_lock);
    return err;
}

int hub_min_rssi(void)
{
    return s_min_rssi;
}

esp_err_t hub_set_min_rssi(int dbm)
{
    if (dbm < HUB_MIN_RSSI_LOWEST || dbm > HUB_MIN_RSSI_HIGHEST) return ESP_ERR_INVALID_ARG;
    if (dbm == s_min_rssi) return ESP_OK;
    s_min_rssi = dbm;
    settings_save_min_rssi(dbm);
    ESP_LOGI(TAG, "noise threshold now %d dBm", dbm);
    return ESP_OK;
}

esp_err_t hub_scan_begin(void)
{
    if (!s_radio_ok) return ESP_ERR_INVALID_STATE;
    // Taking the transmit lock waits out any transmission in progress.
    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_scanning) {
        s_scan_was_listening = radio_listening();
        err = radio_set_listening(false);
        if (err == ESP_OK) s_scanning = true;
    }
    xSemaphoreGive(s_tx_lock);
    return err;
}

void hub_scan_end(void)
{
    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    // The scan only retuned the chip; configuring it again restores the receiver's frequency.
    cc1101_config_t radio = hub_radio();
    esp_err_t err = cc1101_configure(&radio);
    if (err == ESP_OK && s_scan_was_listening) err = radio_set_listening(true);
    if (err != ESP_OK) ESP_LOGE(TAG, "couldn't restore the receiver after a scan: %s", esp_err_to_name(err));
    s_scanning = false;
    xSemaphoreGive(s_tx_lock);
}

bool hub_scanning(void)
{
    return s_scanning;
}

cc1101_config_t hub_radio(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cc1101_config_t c = s_radio;
    xSemaphoreGive(s_lock);
    return c;
}

bool hub_radio_ok(void)
{
    return s_radio_ok;
}

size_t hub_last_capture(uint32_t **out)
{
    *out = NULL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = s_last_count;
    if (n) {
        *out = malloc(n * sizeof(uint32_t));
        if (*out) memcpy(*out, s_last, n * sizeof(uint32_t));
        else n = 0;
    }
    xSemaphoreGive(s_lock);
    return n;
}

esp_err_t hub_transmit(const uint32_t *durations, size_t count)
{
    if (!s_radio_ok) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_scanning) {
        status_led_flash(STATUS_FLASH_TX);
        err = radio_transmit(durations, count);
    }
    xSemaphoreGive(s_tx_lock);
    return err;
}

static bool same_radio(const cc1101_config_t *a, const cc1101_config_t *b)
{
    return a->frequency_hz == b->frequency_hz && a->modulation == b->modulation;
}

esp_err_t hub_transmit_on(const cc1101_config_t *radio, const uint32_t *durations, size_t count)
{
    if (!s_radio_ok) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    if (s_scanning) {
        xSemaphoreGive(s_tx_lock);
        return ESP_ERR_INVALID_STATE;
    }
    cc1101_config_t receiver = hub_radio();
    bool switch_radio = !same_radio(radio, &receiver);
    esp_err_t err = switch_radio ? cc1101_configure(radio) : ESP_OK;
    if (err == ESP_OK) {
        status_led_flash(STATUS_FLASH_TX);
        err = radio_transmit(durations, count);
    }
    if (switch_radio) {
        esp_err_t back = cc1101_configure(&receiver);
        if (back == ESP_OK && radio_listening()) back = radio_set_listening(true);
        if (err == ESP_OK) err = back;
    }
    xSemaphoreGive(s_tx_lock);
    return err;
}

static void keep_last(const radio_capture_t *c)
{
    uint32_t *copy = malloc(c->count * sizeof(uint32_t));
    if (!copy) return;
    memcpy(copy, c->durations, c->count * sizeof(uint32_t));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    free(s_last);
    s_last = copy;
    s_last_count = c->count;
    xSemaphoreGive(s_lock);
}

static void log_capture(const capture_t *c)
{
    const fw_timing_t *t = &c->timing;
    ESP_LOGI(TAG, "capture #%" PRIu32 ": %u marks, %" PRIu32 " ms, short %" PRIu32 " µs, long %" PRIu32
                  " µs, gap %" PRIu32 " µs, RSSI %d dBm",
             c->id, (unsigned)t->pulse_count, t->total_us / 1000, t->short_us, t->long_us, t->gap_us, c->rssi_dbm);
    for (size_t i = 0; i < c->match_count; i++) {
        const fw_match_t *m = &c->matches[i];
        ESP_LOGI(TAG, "  %s: 0x%0*llX (%u bits, %u repeats, base %u µs, %d%%)", m->protocol->id,
                 (m->bit_count + 3) / 4, (unsigned long long)m->bits, m->bit_count, m->repeats, m->base_us,
                 (int)(m->confidence * 100 + 0.5f));
    }
    if (!c->match_count) ESP_LOGI(TAG, "  no decoder matched; kept as raw");
}

// Bursts dropped as noise are logged one by one, up to FILTER_LOG_PER_S a
// second so a noisy band can't bury the console, and reported to the web app
// as a summary every FILTER_REPORT_MS.
#define FILTER_LOG_PER_S 5
#define FILTER_REPORT_MS 2000

typedef struct {
    unsigned count;
    int strongest;
} filter_tally_t;

static void tally(filter_tally_t *t, int rssi)
{
    if (t->count++ == 0 || rssi > t->strongest) t->strongest = rssi;
}

static void capture_task(void *arg)
{
    filter_tally_t second = {0}, hidden = {0}, report = {0};
    int64_t second_start = esp_timer_get_time(), report_start = second_start;
    for (;;) {
        radio_capture_t rf;
        bool got = radio_next_capture(&rf, pdMS_TO_TICKS(250));

        int64_t now = esp_timer_get_time();
        if (now - second_start >= 1000000) {
            if (hidden.count) {
                ESP_LOGI(TAG, "filtered %u more as noise in the last second (strongest %d dBm)", hidden.count,
                         hidden.strongest);
            }
            second = hidden = (filter_tally_t){0};
            second_start = now;
        }
        if (now - report_start >= FILTER_REPORT_MS * 1000LL) {
            if (report.count) {
                api_broadcast_filtered(report.count, report.strongest, hub_min_rssi(),
                                       (uint32_t)((now - report_start) / 1000));
            }
            report = (filter_tally_t){0};
            report_start = now;
        }
        if (!got) continue;

        // Background noise: weak and undecodable.
        fw_match_t probe;
        int threshold = hub_min_rssi();
        if (rf.rssi_dbm < threshold && fw_decode(rf.durations, rf.count, &probe, 1) == 0) {
            tally(&report, rf.rssi_dbm);
            if (second.count < FILTER_LOG_PER_S) {
                ESP_LOGI(TAG, "filtered as noise: %u edges at %d dBm (threshold %d dBm)", (unsigned)rf.count,
                         rf.rssi_dbm, threshold);
                tally(&second, rf.rssi_dbm);
            } else {
                tally(&hidden, rf.rssi_dbm);
            }
            radio_capture_free(&rf);
            continue;
        }
        keep_last(&rf);
        cc1101_config_t radio = hub_radio();
        capture_t c;
        captures_add(&rf, &radio, &c);
        if (c.match_count) status_led_flash(STATUS_FLASH_RX);
        log_capture(&c);
        api_broadcast_capture(&c);
        api_broadcast_fan(fans_observe(&c));
    }
}

static void start_radio(void)
{
    cc1101_pins_t pins = {
        .host = SPI2_HOST,
        .sck = CONFIG_FANWAVE_PIN_SCK,
        .mosi = CONFIG_FANWAVE_PIN_MOSI,
        .miso = CONFIG_FANWAVE_PIN_MISO,
        .cs = CONFIG_FANWAVE_PIN_CS,
    };
    s_min_rssi = settings_load_min_rssi(CONFIG_FANWAVE_RF_MIN_RSSI);
    if (s_min_rssi < HUB_MIN_RSSI_LOWEST || s_min_rssi > HUB_MIN_RSSI_HIGHEST) s_min_rssi = CONFIG_FANWAVE_RF_MIN_RSSI;
    s_radio = cc1101_default_config();
    s_radio.frequency_hz = (uint32_t)CONFIG_FANWAVE_RF_FREQUENCY_KHZ * 1000;

    esp_err_t err = cc1101_init(&pins);
    if (err == ESP_OK) err = cc1101_configure(&s_radio);
    if (err == ESP_OK) {
        radio_config_t rf = {
            .rx_gpio = CONFIG_FANWAVE_PIN_GDO2,
            .tx_gpio = CONFIG_FANWAVE_PIN_GDO0,
            .idle_us = CONFIG_FANWAVE_RF_IDLE_US,
        };
        err = radio_init(&rf);
    }
    if (err == ESP_OK) err = radio_set_listening(true);
    if (err != ESP_OK) {
        // Keep going: Wi-Fi, the API and the console still help diagnose it.
        ESP_LOGE(TAG, "radio failed to start: %s", esp_err_to_name(err));
        status_led_set_error(true);
        return;
    }
    s_radio_ok = true;
    xTaskCreate(capture_task, "capture", 6144, NULL, 5, NULL);
}

void app_main(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_tx_lock = xSemaphoreCreateMutex();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    if (status_led_init(CONFIG_FANWAVE_PIN_LED) == ESP_OK) {
        status_settings_t led = settings_load_led();
        status_led_set_settings(&led);
    } else {
        ESP_LOGW(TAG, "status LED unavailable");
    }

    ESP_ERROR_CHECK(storage_init());
    captures_init();
    ESP_ERROR_CHECK(fans_init());
    start_radio();
    ESP_ERROR_CHECK(wifi_start());
    ESP_ERROR_CHECK(api_start());
    console_start();
}
