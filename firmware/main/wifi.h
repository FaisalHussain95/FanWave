// Wi-Fi: join the saved home network, or run a setup access point
// ("FanWave-XXXX", http://192.168.4.1) when there is none or it can't be reached.

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "esp_err.h"

esp_err_t wifi_start(void);

/** WifiNetwork[], strongest first. */
cJSON *wifi_scan_json(void);

/**
 * Join a network, waiting up to 15 s. On success writes the new IP address
 * to address; on failure writes a reason to err. Both at least 64 bytes.
 */
esp_err_t wifi_connect(const char *ssid, const char *password, char *address, char *err);

typedef struct {
    bool connected;
    bool ap_active;
    char ssid[33];
    char address[16];
    int rssi;
    char hostname[24]; // also the access point's SSID
} wifi_status_t;

wifi_status_t wifi_status(void);
