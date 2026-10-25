// Rooms and fans: storage, pairing, sending commands, and noticing when a
// physical remote sends one of a fan's codes.
//
// Fans are kept as the JSON documents the API serves (see docs/api.md), plus a
// "commands" object holding each learned button.

#pragma once

#include <stdbool.h>

#include "cJSON.h"
#include "captures.h"
#include "cc1101.h"
#include "esp_err.h"

esp_err_t fans_init(void);

/** Copies; the caller deletes them. */
cJSON *fans_rooms_json(void);
cJSON *fans_list_json(void);

/** Each function fills err (at least 96 bytes) on failure. */
cJSON *fans_add_room(const char *name, char *err);
cJSON *fans_add(const cJSON *new_fan, const cc1101_config_t *radio, char *err);
cJSON *fans_command(const char *id, const cJSON *patch, char *err);

/** If the capture matches a learned code, update that fan's confirmed state and return a copy of it. */
cJSON *fans_observe(const capture_t *capture);

/** {version: 1, rooms, fans}, with raw codes inlined. */
cJSON *fans_backup_json(void);
esp_err_t fans_restore(const cJSON *backup, char *err);
