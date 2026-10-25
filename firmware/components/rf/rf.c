#include "rf.h"
#include "rf_segment.h"

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
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

static const char *TAG = "rf";

// Receiving streams. The RMT only ends a receive on silence (idle_us with no
// edge), but a CC1101 in asynchronous mode turns background noise into a
// constant stream of edges on GDO2, so the line is almost never silent. In DMA
// mode a receive that never ends reports nothing at all: the circular DMA
// buffer just keeps overwriting itself. So the receive runs with en_partial_rx,
// every filled DMA block is handed over as it completes, and rx_task cuts the
// stream into captures itself: on silence, or once a capture reaches
// MAX_EDGES or MAX_CAPTURE_US. Noise then arrives as weak, undecodable
// captures, which the hub's noise threshold drops.

#define RES_HZ 1000000       // 1 tick = 1 µs
#define RX_SYMBOLS 2048      // RMT/DMA receive buffer, split into ~3 DMA blocks
#define MAX_TICKS 32767      // an RMT symbol half holds 15 bits
#define GLITCH_NS 3000       // the RMT input filter tops out around 3.2 µs
#define RSSI_SAMPLE_MS 2
#define CAPTURE_QUEUE 4
#define STREAM_BYTES (32 * 1024)
#define MAX_EDGES 4096       // longest capture, in durations
#define MAX_CAPTURE_US 300000 // a held button sends frames continuously; cut it into pieces this long

static radio_config_t s_cfg;
static rmt_channel_handle_t s_rx, s_tx;
static rmt_encoder_handle_t s_copy;
static rmt_symbol_word_t *s_rx_buf;
static StreamBufferHandle_t s_stream; // ISR → task: received RMT symbols
static QueueHandle_t s_done;          // ISR → task: the receive ended on silence; re-arm it
static QueueHandle_t s_captures;      // task → consumers
static SemaphoreHandle_t s_radio;     // held while transmitting
static volatile bool s_listening;
static volatile bool s_overflow;      // symbols were lost; the capture in progress is broken
static volatile uint32_t s_edge_rate; // GDO2 edges in the last full second

static bool IRAM_ATTR on_recv_done(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *ev, void *ctx)
{
    BaseType_t woken = pdFALSE;
    // Only whole symbols, so a full stream can't leave half of one behind.
    size_t bytes = ev->num_symbols * sizeof(rmt_symbol_word_t);
    size_t room = xStreamBufferSpacesAvailable(s_stream) / sizeof(rmt_symbol_word_t) * sizeof(rmt_symbol_word_t);
    if (bytes > room) {
        bytes = room;
        s_overflow = true;
    }
    if (bytes) xStreamBufferSendFromISR(s_stream, ev->received_symbols, bytes, &woken);
    if (ev->flags.is_last) {
        uint8_t one = 1;
        xQueueSendFromISR(s_done, &one, &woken);
    }
    return woken == pdTRUE;
}

static rf_segment_t s_seg; // the capture being assembled from the stream
static int s_seg_rssi;      // loudest RSSI sampled since the previous capture ended

static void push_capture(const radio_capture_t *c)
{
    if (xQueueSend(s_captures, c, 0) == pdTRUE) return;
    // Full: drop the oldest so the newest press is never lost.
    radio_capture_t old;
    if (xQueueReceive(s_captures, &old, 0) == pdTRUE) radio_capture_free(&old);
    xQueueSend(s_captures, c, 0);
}

/** rf_segment_t's emit callback: hand a finished capture to the consumers. */
static void emit_capture(const uint32_t *d, size_t n, void *ctx)
{
    int rssi = s_seg_rssi;
    s_seg_rssi = -128;
    if ((n + 1) / 2 < s_cfg.min_marks) return;
    radio_capture_t c = {
        .durations = malloc(n * sizeof(uint32_t)),
        .count = n,
        .rssi_dbm = rssi,
        .at_us = esp_timer_get_time(),
    };
    if (!c.durations) return;
    memcpy(c.durations, d, n * sizeof(uint32_t));
    push_capture(&c);
}

static void feed_symbols(const rmt_symbol_word_t *sym, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        rf_segment_feed(&s_seg, sym[i].duration0, sym[i].level0, emit_capture, NULL);
        if (sym[i].duration0) rf_segment_feed(&s_seg, sym[i].duration1, sym[i].level1, emit_capture, NULL);
    }
}

static void sample_rssi(void)
{
    if (xSemaphoreTake(s_radio, 0) != pdTRUE) return;
    int now = cc1101_rssi_dbm();
    xSemaphoreGive(s_radio);
    if (now > s_seg_rssi) s_seg_rssi = now;
}

static void rx_task(void *arg)
{
    rmt_receive_config_t rc = {
        .signal_range_min_ns = GLITCH_NS,
        .signal_range_max_ns = s_cfg.idle_us * 1000,
        .flags.en_partial_rx = true,
    };
    rmt_symbol_word_t chunk[256];
    bool armed = false;
    uint32_t edges = 0;
    int64_t rate_start = esp_timer_get_time();
    uint32_t *buf = malloc(MAX_EDGES * sizeof(uint32_t));
    assert(buf);
    rf_segment_init(&s_seg, buf, MAX_EDGES, MAX_CAPTURE_US);
    s_seg_rssi = -128;

    for (;;) {
        if (!s_listening) {
            if (armed) {
                // Abort the receive so it can be re-armed later.
                rmt_disable(s_rx);
                rmt_enable(s_rx);
                armed = false;
            }
            xQueueReset(s_done);
            xStreamBufferReset(s_stream);
            s_overflow = false;
            rf_segment_reset(&s_seg);
            s_edge_rate = 0;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (!armed) {
            if (rmt_receive(s_rx, s_rx_buf, RX_SYMBOLS * sizeof(rmt_symbol_word_t), &rc) != ESP_OK) {
                ESP_LOGE(TAG, "couldn't start receiving");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            armed = true;
        }

        // Wait at most RSSI_SAMPLE_MS for symbols, so RSSI keeps being sampled
        // while a burst is on air: once it's over the register only shows noise.
        size_t got = xStreamBufferReceive(s_stream, chunk, sizeof(chunk), pdMS_TO_TICKS(RSSI_SAMPLE_MS));
        sample_rssi();
        if (s_overflow) {
            s_overflow = false;
            ESP_LOGW(TAG, "receive stream overflowed; dropped the capture in progress");
            s_seg_rssi = -128;
            rf_segment_reset(&s_seg);
        }
        feed_symbols(chunk, got / sizeof(rmt_symbol_word_t));
        edges += got / sizeof(rmt_symbol_word_t) * 2;

        uint8_t ended;
        if (xQueueReceive(s_done, &ended, 0) == pdTRUE) {
            // Silence ended the receive. Take what's left in the stream, then re-arm.
            while ((got = xStreamBufferReceive(s_stream, chunk, sizeof(chunk), 0)) > 0) {
                feed_symbols(chunk, got / sizeof(rmt_symbol_word_t));
                edges += got / sizeof(rmt_symbol_word_t) * 2;
            }
            rf_segment_end(&s_seg, emit_capture, NULL);
            armed = false;
        }

        int64_t now = esp_timer_get_time();
        if (now - rate_start >= 1000000) {
            s_edge_rate = edges;
            edges = 0;
            rate_start = now;
        }
    }
}

esp_err_t radio_init(const radio_config_t *config)
{
    s_cfg = *config;
    if (s_cfg.idle_us == 0 || s_cfg.idle_us > MAX_TICKS) s_cfg.idle_us = 20000;
    if (s_cfg.min_marks == 0) s_cfg.min_marks = 16;

    s_done = xQueueCreate(2, sizeof(uint8_t));
    s_stream = xStreamBufferCreate(STREAM_BYTES, sizeof(rmt_symbol_word_t));
    s_captures = xQueueCreate(CAPTURE_QUEUE, sizeof(radio_capture_t));
    s_radio = xSemaphoreCreateMutex();
    s_rx_buf = heap_caps_malloc(RX_SYMBOLS * sizeof(rmt_symbol_word_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_done && s_stream && s_captures && s_radio && s_rx_buf, ESP_ERR_NO_MEM, TAG, "alloc");

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

    BaseType_t ok = xTaskCreate(rx_task, "rf_rx", 6144, NULL, 10, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "RX task");
    return ESP_OK;
}

esp_err_t radio_set_listening(bool on)
{
    xSemaphoreTake(s_radio, portMAX_DELAY);
    esp_err_t err = on ? cc1101_rx() : cc1101_idle();
    if (err == ESP_OK) s_listening = on;
    xSemaphoreGive(s_radio);
    return err;
}

bool radio_listening(void)
{
    return s_listening;
}

uint32_t radio_edge_rate(void)
{
    return s_edge_rate;
}

bool radio_next_capture(radio_capture_t *out, TickType_t wait)
{
    return xQueueReceive(s_captures, out, wait) == pdTRUE;
}

void radio_capture_free(radio_capture_t *c)
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

esp_err_t radio_transmit(const uint32_t *durations, size_t count)
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
