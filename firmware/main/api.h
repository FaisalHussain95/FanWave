// HTTP API, WebSocket events and web app hosting. See docs/api.md.

#pragma once

#include "cJSON.h"
#include "captures.h"
#include "esp_err.h"

esp_err_t api_start(void);

/** Send a message to every /api/events client. Takes ownership. */
void api_broadcast(cJSON *message);
void api_broadcast_fan(cJSON *fan);
void api_broadcast_capture(const capture_t *capture);
