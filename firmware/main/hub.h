// State shared between the capture loop, the console and the HTTP API.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cc1101.h"
#include "esp_err.h"

/** Reconfigure the radio, keeping the listening state. Refused while scanning. */
esp_err_t hub_set_radio(const cc1101_config_t *config);
cc1101_config_t hub_radio(void);

/** Copy of the most recent capture. Returns 0 if none; caller frees *out. */
size_t hub_last_capture(uint32_t **out);

/** Transmit a pulse train, flashing the status LED. */
esp_err_t hub_transmit(const uint32_t *durations, size_t count);

/** Transmit on another frequency/modulation, then put the receiver back. */
esp_err_t hub_transmit_on(const cc1101_config_t *radio, const uint32_t *durations, size_t count);

/** True once the CC1101 answered and the receiver started. */
bool hub_radio_ok(void);

/** Start or stop capturing. Refused while scanning. */
esp_err_t hub_set_listening(bool on);

#define HUB_MIN_RSSI_LOWEST (-110)
#define HUB_MIN_RSSI_HIGHEST (-30)

/**
 * Undecoded bursts weaker than this (dBm) are dropped as noise. Starts at
 * CONFIG_FANWAVE_RF_MIN_RSSI; a value set here is saved and wins after restarts.
 */
int hub_min_rssi(void);
/** ESP_ERR_INVALID_ARG outside HUB_MIN_RSSI_LOWEST..HUB_MIN_RSSI_HIGHEST. */
esp_err_t hub_set_min_rssi(int dbm);

/**
 * Lend the radio to the frequency scan (freq_scan.c): capturing stops, and radio
 * changes and transmits fail with ESP_ERR_INVALID_STATE until hub_scan_end()
 * puts the receiver back as it was.
 */
esp_err_t hub_scan_begin(void);
void hub_scan_end(void);
bool hub_scanning(void);

/** Start the serial console (console.c). */
void console_start(void);
