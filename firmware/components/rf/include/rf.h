// Radio capture and replay over the ESP32-S3 RMT peripheral.
//
// The CC1101 runs in asynchronous serial mode: in RX it outputs the
// demodulated signal on GDO2, which the RMT receiver timestamps; in TX it
// transmits whatever the RMT transmitter drives onto GDO0.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int rx_gpio;          // CC1101 GDO2
    int tx_gpio;          // CC1101 GDO0
    uint32_t idle_us;     // silence that ends a capture; must exceed the longest sync gap
    uint32_t min_marks;   // captures with fewer marks are dropped as noise
} rf_config_t;

/** One burst of activity: durations in µs, alternating mark/space, starting with a mark. */
typedef struct {
    uint32_t *durations;
    size_t count;
    int rssi_dbm;   // strongest RSSI sampled while the burst was arriving
    int64_t at_us;  // esp_timer time when it ended
} rf_capture_t;

esp_err_t rf_init(const rf_config_t *config);

/** Start or stop listening. Puts the CC1101 in RX or idle. */
esp_err_t rf_set_listening(bool on);
bool rf_listening(void);

/** Wait for the next capture. On success the caller owns it and must call rf_capture_free(). */
bool rf_next_capture(rf_capture_t *out, TickType_t wait);
void rf_capture_free(rf_capture_t *capture);

/**
 * Transmit a pulse train (same format as a capture) once, blocking until it
 * has been sent. Listening resumes afterwards if it was on.
 */
esp_err_t rf_transmit(const uint32_t *durations, size_t count);

#ifdef __cplusplus
}
#endif
