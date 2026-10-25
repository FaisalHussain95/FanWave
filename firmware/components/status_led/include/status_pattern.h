// What the status LED shows at a given moment, as a pure function of the
// hub's state, the user's settings and the time. No hardware access, so it is
// tested on the host (test/host). The table lives in docs/architecture.md.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STATUS_SETUP_AP = 0, // running the hub's own access point for onboarding
    STATUS_CONNECTING,   // joining the home Wi-Fi
    STATUS_ONLINE,
} status_base_t;

typedef enum {
    STATUS_FLASH_NONE = 0,
    STATUS_FLASH_TX,      // sent a command
    STATUS_FLASH_RX,      // heard a remote
    STATUS_FLASH_SUCCESS, // learned a button, finished an update
} status_flash_t;

typedef struct {
    bool enabled;       // false: errors (and Identify) only
    uint8_t brightness; // 0–100
    bool idle_dim;      // online and idle: dim accent, otherwise off
    bool radio_flashes; // show TX/RX flashes
} status_settings_t;

typedef struct {
    status_base_t base;
    bool error;
    bool updating;
    bool learning;
    status_flash_t flash;
    int64_t flash_until_ms;
    int64_t identify_until_ms;
} status_state_t;

typedef struct {
    uint8_t r, g, b;
} status_rgb_t;

status_settings_t status_default_settings(void);

/** How long each kind of flash lasts. */
uint32_t status_flash_ms(status_flash_t flash);

/** The colour to show at time now_ms, already scaled by brightness. */
status_rgb_t status_pattern(const status_state_t *state, const status_settings_t *settings, int64_t now_ms);

#ifdef __cplusplus
}
#endif
