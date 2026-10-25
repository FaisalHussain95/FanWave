// Small settings kept in NVS.

#pragma once

#include "status_pattern.h"

status_settings_t settings_load_led(void);
void settings_save_led(const status_settings_t *s);

/** Noise threshold for undecoded bursts (hub_min_rssi()); fallback when none is saved. */
int settings_load_min_rssi(int fallback);
void settings_save_min_rssi(int dbm);
