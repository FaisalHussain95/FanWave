#include "rf.h"

#include <stdlib.h>
#include <string.h>

#include "cc1101.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "rf";

#define RES_HZ 1000000       // 1 tick = 1 µs
#define RX_SYMBOLS 2048      // one capture holds up to 4096 edges
#define MAX_TICKS 32767      // an RMT symbol half holds 15 bits
#define GLITCH_NS 3000       // the RMT input filter tops out around 3.2 µs
#define RSSI_SAMPLE_MS 2
#define CAPTURE_QUEUE 4

static rf_config_t s_cfg;
static rmt_channel_handle_t s_rx, s_tx;
static rmt_encoder_handle_t s_copy;
static rmt_symbol_word_t *s_rx_buf;
static QueueHandle_t s_done;      // ISR → task: symbol count of a finished receive
static QueueHandle_t s_captures;  // task → consumers
static SemaphoreHandle_t s_radio; // held while transmitting
static volatile bool s_listening;

static bool IRAM_ATTR on_recv_done(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *ev, void *ctx)
{
    BaseType_t woken = pdFALSE;
    size_t n = ev->num_symbols;
    xQueueSendFromISR(s_done, &n, &woken);
    return woken == pdTRUE;
}

/**
 * Turn RMT symbols into alternating mark/space durations starting with a
 * mark: drop leading silence, merge runs of the same level, and stop at the
 * zero-length end marker.
 */
static size_t to_durations(const rmt_symbol_word_t *sym, size_t nsym, uint32_t *out)
{
    size_t n = 0;
    int level = -1;
    for (size_t i = 0; i < nsym; i++) {
        uint32_t d[2] = {sym[i].duration0, sym[i].duration1};
        int l[2] = {sym[i].level0, sym[i].level1};
        for (int h = 0; h < 2; h++) {
            if (d[h] == 0) return n;
            if (n == 0 && l[h] == 0) continue;
            if (l[h] == level) {
                out[n - 1] += d[h];
            } else {
                out[n++] = d[h];
                level = l[h];
            }
        }
    }
    return n;
}

static void push_capture(const rf_capture_t *c)
{
    if (xQueueSend(s_captures, c, 0) == pdTRUE) return;
    // Full: drop the oldest so the newest press is never lost.
    rf_capture_t old;
    if (xQueueReceive(s_captures, &old, 0) == pdTRUE) rf_capture_free(&old);
    xQueueSend(s_captures, c, 0);
}

static void rx_task(void *arg)
{
    rmt_receive_config_t rc = {
        .signal_range_min_ns = GLITCH_NS,
        .signal_range_max_ns = s_cfg.idle_us * 1000,
    };
    uint32_t *durations = malloc(RX_SYMBOLS * 2 * sizeof(uint32_t));
    assert(durations);

    for (;;) {
        if (!s_listening) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (rmt_receive(s_rx, s_rx_buf, RX_SYMBOLS * sizeof(rmt_symbol_word_t), &rc) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Sample RSSI while the burst arrives: by the time it ends the
        // carrier is gone and the register only shows the noise floor.
        size_t nsym = 0;
        int rssi = -128;
        while (xQueueReceive(s_done, &nsym, pdMS_TO_TICKS(RSSI_SAMPLE_MS)) != pdTRUE) {
            if (!s_listening) {
                // Abort the armed receive so it can be re-armed later.
                rmt_disable(s_rx);
                rmt_enable(s_rx);
                xQueueReset(s_done);
                break;
            }
            if (xSemaphoreTake(s_radio, 0) == pdTRUE) {
                int now = cc1101_rssi_dbm();
                if (now > rssi) rssi = now;
                xSemaphoreGive(s_radio);
            }
        }
        if (!nsym) continue;

        size_t n = to_durations(s_rx_buf, nsym, durations);
        if ((n + 1) / 2 < s_cfg.min_marks) continue;

        rf_capture_t c = {
            .durations = malloc(n * sizeof(uint32_t)),
            .count = n,
            .rssi_dbm = rssi,
            .at_us = esp_timer_get_time(),
        };
        if (!c.durations) continue;
        memcpy(c.durations, durations, n * sizeof(uint32_t));
        push_capture(&c);
    }
}

esp_err_t rf_init(const rf_config_t *config)
{
    s_cfg = *config;
    if (s_cfg.idle_us == 0 || s_cfg.idle_us > MAX_TICKS) s_cfg.idle_us = 20000;
    if (s_cfg.min_marks == 0) s_cfg.min_marks = 16;

    s_done = xQueueCreate(1, sizeof(size_t));
    s_captures = xQueueCreate(CAPTURE_QUEUE, sizeof(rf_capture_t));
    s_radio = xSemaphoreCreateMutex();
    s_rx_buf = heap_caps_malloc(RX_SYMBOLS * sizeof(rmt_symbol_word_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_done && s_captures && s_radio && s_rx_buf, ESP_ERR_NO_MEM, TAG, "alloc");

    rmt_rx_channel_config_t rx = {
        .gpio_num = s_cfg.rx_gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RES_HZ,
        .mem_block_symbols = RX_SYMBOLS,
        .flags.with_dma = true, // long captures don't fit in RMT RAM
    };
    ESP_RETURN_ON_ERROR(rmt_new_rx_channel(&rx, &s_rx), TAG, "RX channel");
    rmt_rx_event_callbacks_t cbs = {.on_recv_done = on_recv_done};
    ESP_RETURN_ON_ERROR(rmt_rx_register_event_callbacks(s_rx, &cbs, NULL), TAG, "RX callbacks");
    ESP_RETURN_ON_ERROR(rmt_enable(s_rx), TAG, "RX enable");

    rmt_tx_channel_config_t tx = {
        .gpio_num = s_cfg.tx_gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RES_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 2,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx, &s_tx), TAG, "TX channel");
    rmt_copy_encoder_config_t copy = {};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&copy, &s_copy), TAG, "copy encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(s_tx), TAG, "TX enable");

    BaseType_t ok = xTaskCreate(rx_task, "rf_rx", 4096, NULL, 10, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "RX task");
    return ESP_OK;
}

esp_err_t rf_set_listening(bool on)
{
    xSemaphoreTake(s_radio, portMAX_DELAY);
    esp_err_t err = on ? cc1101_rx() : cc1101_idle();
    if (err == ESP_OK) s_listening = on;
    xSemaphoreGive(s_radio);
    return err;
}

bool rf_listening(void)
{
    return s_listening;
}

bool rf_next_capture(rf_capture_t *out, TickType_t wait)
{
    return xQueueReceive(s_captures, out, wait) == pdTRUE;
}

void rf_capture_free(rf_capture_t *c)
{
    free(c->durations);
    c->durations = NULL;
    c->count = 0;
}

/** Pack durations into RMT symbols, splitting any longer than an RMT half can hold. */
static size_t to_symbols(const uint32_t *d, size_t n, rmt_symbol_word_t *sym, size_t max)
{
    size_t s = 0;
    int half = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t left = d[i];
        unsigned level = i % 2 == 0; // marks are high
        while (left) {
            uint32_t chunk = left > MAX_TICKS ? MAX_TICKS : left;
            left -= chunk;
            if (half == 0) {
                if (s == max) return 0;
                sym[s] = (rmt_symbol_word_t){.level0 = level, .duration0 = chunk};
                half = 1;
            } else {
                sym[s].level1 = level;
                sym[s].duration1 = chunk;
                s++;
                half = 0;
            }
        }
    }
    if (half) {
        // Pad the last symbol with a short low so the line ends idle.
        sym[s].level1 = 0;
        sym[s].duration1 = 1;
        s++;
    }
    return s;
}

esp_err_t rf_transmit(const uint32_t *durations, size_t count)
{
    ESP_RETURN_ON_FALSE(durations && count, ESP_ERR_INVALID_ARG, TAG, "empty pulse train");
    size_t max = count + 16;
    for (size_t i = 0; i < count; i++) max += durations[i] / MAX_TICKS;
    rmt_symbol_word_t *sym = malloc(max * sizeof(rmt_symbol_word_t));
    ESP_RETURN_ON_FALSE(sym, ESP_ERR_NO_MEM, TAG, "alloc");
    size_t nsym = to_symbols(durations, count, sym, max);

    xSemaphoreTake(s_radio, portMAX_DELAY);
    bool was_listening = s_listening;
    esp_err_t err = nsym ? cc1101_tx() : ESP_ERR_INVALID_SIZE;
    if (err == ESP_OK) {
        // Leaving idle triggers auto-calibration (~800 µs) before the PA is up.
        esp_rom_delay_us(1000);
        rmt_transmit_config_t tc = {.loop_count = 0, .flags.eot_level = 0};
        err = rmt_transmit(s_tx, s_copy, sym, nsym * sizeof(rmt_symbol_word_t), &tc);
        if (err == ESP_OK) err = rmt_tx_wait_all_done(s_tx, 5000);
    }
    esp_err_t after = was_listening ? cc1101_rx() : cc1101_idle();
    xSemaphoreGive(s_radio);
    free(sym);
    ESP_RETURN_ON_ERROR(err, TAG, "transmit");
    return after;
}
