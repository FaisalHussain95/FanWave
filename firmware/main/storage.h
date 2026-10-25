// Flash storage: the web app's files on /www, fans, rooms and learned codes on /data.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"

#define WWW_ROOT "/www"
#define DATA_ROOT "/data"

esp_err_t storage_init(void);

/** Parse a JSON file. NULL if it doesn't exist or doesn't parse. */
cJSON *storage_read_json(const char *path);

/** Write JSON through a temporary file so a power cut can't leave half a file. */
esp_err_t storage_write_json(const char *path, const cJSON *json);

/** Raw learned codes, stored as little-endian uint16 µs (clamped). */
esp_err_t storage_write_durations(const char *path, const uint32_t *d, size_t count);
size_t storage_read_durations(const char *path, uint32_t **out);

/** Free space on /data in bytes. */
size_t storage_free(void);
