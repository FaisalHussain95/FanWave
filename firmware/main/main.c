// FanWave firmware, first milestone: bring up the CC1101, capture and decode
// remote presses, and replay or encode them from a serial console. Wi-Fi and
// the HTTP/WebSocket API for the web app come next.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cc1101.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "fw_protocol.h"
#include "hub.h"
#include "nvs_flash.h"
#include "rf.h"
#include "sdkconfig.h"
#include "status_led.h"

static const char *TAG = "fanwave";

static SemaphoreHandle_t s_lock;
static cc1101_config_t s_radio;
static uint32_t *s_last;
static size_t s_last_count;

esp_err_t hub_set_radio(const cc1101_config_t *config)
{
    bool listening = rf_listening();
    esp_err_t err = rf_set_listening(false);
    if (err == ESP_OK) err = cc1101_configure(config);
    if (err == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_radio = *config;
        xSemaphoreGive(s_lock);
    }
    if (listening) {
        esp_err_t rx = rf_set_listening(true);
        if (err == ESP_OK) err = rx;
    }
    return err;
}

cc1101_config_t hub_radio(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cc1101_config_t c = s_radio;
    xSemaphoreGive(s_lock);
    return c;
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
    status_led_flash(STATUS_FLASH_TX);
    return rf_transmit(durations, count);
}

static void keep_last(rf_capture_t *c)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    free(s_last);
    s_last = c->durations;
    s_last_count = c->count;
    c->durations = NULL;
    xSemaphoreGive(s_lock);
}

static void capture_task(void *arg)
{
    fw_match_t matches[4];
    for (;;) {
        rf_capture_t c;
        if (!rf_next_capture(&c, portMAX_DELAY)) continue;

        fw_timing_t t = fw_analyze(c.durations, c.count);
        size_t n = fw_decode(c.durations, c.count, matches, 4);
        if (n) status_led_flash(STATUS_FLASH_RX);

        ESP_LOGI(TAG, "capture: %u marks, %" PRIu32 " ms, short %" PRIu32 " µs, long %" PRIu32
                      " µs, gap %" PRIu32 " µs, RSSI %d dBm",
                 (unsigned)t.pulse_count, t.total_us / 1000, t.short_us, t.long_us, t.gap_us, c.rssi_dbm);
        for (size_t i = 0; i < n; i++) {
            const fw_match_t *m = &matches[i];
            ESP_LOGI(TAG, "  %s: 0x%0*llX (%u bits, %u repeats, base %u µs, %d%%)", m->protocol->id,
                     (m->bit_count + 3) / 4, (unsigned long long)m->bits, m->bit_count, m->repeats, m->base_us,
                     (int)(m->confidence * 100 + 0.5f));
        }
        if (!n) ESP_LOGI(TAG, "  no decoder matched; kept as raw (use \"replay\")");
        keep_last(&c);
        rf_capture_free(&c);
    }
}

void app_main(void)
{
    s_lock = xSemaphoreCreateMutex();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    if (status_led_init(CONFIG_FANWAVE_PIN_LED) != ESP_OK) ESP_LOGW(TAG, "status LED unavailable");

    cc1101_pins_t pins = {
        .host = SPI2_HOST,
        .sck = CONFIG_FANWAVE_PIN_SCK,
        .mosi = CONFIG_FANWAVE_PIN_MOSI,
        .miso = CONFIG_FANWAVE_PIN_MISO,
        .cs = CONFIG_FANWAVE_PIN_CS,
    };
    s_radio = cc1101_default_config();
    s_radio.frequency_hz = (uint32_t)CONFIG_FANWAVE_RF_FREQUENCY_KHZ * 1000;

    err = cc1101_init(&pins);
    if (err == ESP_OK) err = cc1101_configure(&s_radio);
    if (err == ESP_OK) {
        rf_config_t rf = {
            .rx_gpio = CONFIG_FANWAVE_PIN_GDO2,
            .tx_gpio = CONFIG_FANWAVE_PIN_GDO0,
            .idle_us = CONFIG_FANWAVE_RF_IDLE_US,
        };
        err = rf_init(&rf);
    }
    if (err == ESP_OK) err = rf_set_listening(true);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "radio failed to start: %s", esp_err_to_name(err));
        status_led_set_error(true);
    } else {
        // No Wi-Fi yet: "online" here just means the radio is up.
        status_led_set_base(STATUS_ONLINE);
        xTaskCreate(capture_task, "capture", 6144, NULL, 5, NULL);
    }
    console_start();
}
