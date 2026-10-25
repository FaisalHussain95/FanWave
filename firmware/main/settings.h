// Small settings kept in NVS.

#pragma once

#include "status_pattern.h"

status_settings_t settings_load_led(void);
void settings_save_led(const status_settings_t *s);
