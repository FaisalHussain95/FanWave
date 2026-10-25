// The last 20 captures, each with its timing summary and decoder matches.
// The API lists them, pairing stores learned buttons from them by id.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "cc1101.h"
#include "fw_protocol.h"
#include "rf.h"

#define CAPTURE_MATCHES 4

typedef struct {
    uint32_t id;
    int64_t at_ms; // since boot
    cc1101_config_t radio;
    int rssi_dbm;
    uint32_t *durations;
    size_t count;
    fw_timing_t timing;
    fw_match_t matches[CAPTURE_MATCHES];
    size_t match_count;
} capture_t;

void captures_init(void);

/** Analyse and store a capture, taking ownership of its durations. Fills *out (durations not copied). */
uint32_t captures_add(radio_capture_t *rf, const cc1101_config_t *radio, capture_t *out);

/** Copy a stored capture, durations included; free with captures_free(). */
bool captures_get(uint32_t id, capture_t *out);
void captures_free(capture_t *c);

/** {now, captures: [...]}, newest first. */
cJSON *captures_list_json(void);

/** ProtocolMatch[] for a capture, or NULL if it's no longer stored. */
cJSON *captures_matches_json(uint32_t id);

cJSON *capture_json(const capture_t *c);
cJSON *match_json(const fw_match_t *m);

/** Milliseconds since boot, the clock captures and the API use. */
int64_t uptime_ms(void);

/** Most significant bit first, e.g. "001110". Writes count+1 bytes. */
void bits_to_string(uint64_t bits, uint8_t count, char *out);
