// The DevKitC-1's addressable RGB LED, driven over RMT, showing hub status.
// Patterns come from status_pattern.c; this file owns the hardware and timing.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "status_pattern.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t status_led_init(int gpio);

void status_led_set_base(status_base_t base);
void status_led_set_error(bool on);
void status_led_set_updating(bool on);
void status_led_set_learning(bool on);
void status_led_flash(status_flash_t flash);
void status_led_identify(uint32_t ms);

void status_led_set_settings(const status_settings_t *settings);
status_settings_t status_led_settings(void);

#ifdef __cplusplus
}
#endif
