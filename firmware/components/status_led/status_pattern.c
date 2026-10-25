#include "status_pattern.h"

#include <math.h>

// Nocturne accent, #9184d9, so the LED matches the app.
static const status_rgb_t ACCENT = {145, 132, 217};
static const status_rgb_t RED = {255, 0, 0};
static const status_rgb_t PURPLE = {150, 0, 255};
static const status_rgb_t BLUE = {0, 60, 255};
static const status_rgb_t GREEN = {0, 255, 40};
static const status_rgb_t WHITE = {255, 255, 255};
static const status_rgb_t OFF = {0, 0, 0};

#define PI_F 3.14159265f

status_settings_t status_default_settings(void)
{
    return (status_settings_t){.enabled = true, .brightness = 40, .idle_dim = true, .radio_flashes = true};
}

uint32_t status_flash_ms(status_flash_t flash)
{
    switch (flash) {
    case STATUS_FLASH_TX:
    case STATUS_FLASH_RX:
        return 80;
    case STATUS_FLASH_SUCCESS:
        return 400;
    default:
        return 0;
    }
}

static status_rgb_t scale(status_rgb_t c, float f)
{
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return (status_rgb_t){(uint8_t)(c.r * f + 0.5f), (uint8_t)(c.g * f + 0.5f), (uint8_t)(c.b * f + 0.5f)};
}

static bool blink(int64_t now_ms, uint32_t period_ms)
{
    return (now_ms % period_ms) < period_ms / 2;
}

/** 0..1..0 over one period, smooth. */
static float breathe(int64_t now_ms, uint32_t period_ms)
{
    float phase = (float)(now_ms % period_ms) / (float)period_ms;
    return 0.5f - 0.5f * cosf(2 * PI_F * phase);
}

status_rgb_t status_pattern(const status_state_t *st, const status_settings_t *set, int64_t now_ms)
{
    float level = set->brightness / 100.0f;

    // Identify and errors show even with the LED turned off, never dimmer than 20%.
    float urgent = level < 0.2f ? 0.2f : level;
    if (now_ms < st->identify_until_ms) return blink(now_ms, 250) ? scale(WHITE, urgent) : OFF;
    if (st->error) return scale(RED, urgent);
    if (!set->enabled || set->brightness == 0) return OFF;

    if (st->updating) return blink(now_ms, 500) ? scale(PURPLE, level) : OFF;

    if (now_ms < st->flash_until_ms) {
        if (st->flash == STATUS_FLASH_SUCCESS) return scale(GREEN, level);
        if (set->radio_flashes && st->flash == STATUS_FLASH_TX) return scale(ACCENT, level);
        if (set->radio_flashes && st->flash == STATUS_FLASH_RX) return scale(WHITE, level);
    }

    if (st->learning) return scale(ACCENT, level * (0.15f + 0.85f * breathe(now_ms, 2000)));

    switch (st->base) {
    case STATUS_SETUP_AP:
        return scale(BLUE, level * (0.1f + 0.9f * breathe(now_ms, 3000)));
    case STATUS_CONNECTING:
        return blink(now_ms, 200) ? scale(BLUE, level) : OFF;
    case STATUS_ONLINE:
    default:
        return set->idle_dim ? scale(ACCENT, level * 0.15f) : OFF;
    }
}
