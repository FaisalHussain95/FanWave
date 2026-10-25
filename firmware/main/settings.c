#include "settings.h"

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "settings";

status_settings_t settings_load_led(void)
{
    status_settings_t s = status_default_settings();
    nvs_handle_t h;
    if (nvs_open("fanwave", NVS_READONLY, &h) != ESP_OK) return s;
    size_t len = sizeof(s);
    status_settings_t stored;
    if (nvs_get_blob(h, "led", &stored, &len) == ESP_OK && len == sizeof(stored)) s = stored;
    nvs_close(h);
    return s;
}

void settings_save_led(const status_settings_t *s)
{
    nvs_handle_t h;
    if (nvs_open("fanwave", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, "led", s, sizeof(*s)) != ESP_OK || nvs_commit(h) != ESP_OK) {
        ESP_LOGW(TAG, "couldn't save LED settings");
    }
    nvs_close(h);
}
