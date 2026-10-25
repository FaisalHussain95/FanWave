// State shared between the capture loop and the console (and, later, the HTTP API).

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cc1101.h"
#include "esp_err.h"

/** Reconfigure the radio, keeping the listening state. */
esp_err_t hub_set_radio(const cc1101_config_t *config);
cc1101_config_t hub_radio(void);

/** Copy of the most recent capture. Returns 0 if none; caller frees *out. */
size_t hub_last_capture(uint32_t **out);

/** Transmit a pulse train, flashing the status LED. */
esp_err_t hub_transmit(const uint32_t *durations, size_t count);

/** Start the serial console (console.c). */
void console_start(void);
