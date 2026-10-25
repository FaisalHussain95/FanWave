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
/** Summary of the bursts dropped as noise over the last period_ms. */
void api_broadcast_filtered(unsigned count, int strongest_dbm, int threshold_dbm, uint32_t period_ms);
