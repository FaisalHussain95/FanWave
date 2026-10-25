#include "wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "sdkconfig.h"
#include "status_led.h"

static const char *TAG = "wifi";

#define CONNECTED_BIT BIT0
#define FAILED_BIT BIT1
#define FALLBACK_MS 20000   // give the home network this long before opening the setup AP
#define AP_GRACE_MS 120000  // keep the AP after joining, so the phone can read the result
#define CONNECT_TIMEOUT_MS 15000
#define MAX_SCAN 20

static EventGroupHandle_t s_events;
static esp_netif_t *s_sta, *s_ap;
static TimerHandle_t s_fallback, s_ap_off, s_retry;
static char s_hostname[24];
static volatile bool s_connected, s_ap_active, s_user_connect;
static volatile wifi_err_reason_t s_last_reason;
static char s_address[16];

static void show_status(void)
{
    status_led_set_base(s_connected ? STATUS_ONLINE : s_ap_active ? STATUS_SETUP_AP : STATUS_CONNECTING);
}

static void start_ap(void)
{
    if (s_ap_active) return;
    wifi_config_t ap = {
        .ap = {.channel = 1, .max_connection = 4, .authmode = WIFI_AUTH_OPEN},
    };
    strlcpy((char *)ap.ap.ssid, s_hostname, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(s_hostname);
    const char *pass = CONFIG_FANWAVE_AP_PASSWORD;
    if (strlen(pass) >= 8) {
        strlcpy((char *)ap.ap.password, pass, sizeof(ap.ap.password));
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    s_ap_active = true;
    ESP_LOGI(TAG, "setup access point \"%s\" up at http://192.168.4.1", s_hostname);
    show_status();
}

static void stop_ap(void)
{
    if (!s_ap_active || !s_connected) return;
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_ap_active = false;
    ESP_LOGI(TAG, "setup access point closed");
    show_status();
}

static void on_fallback(TimerHandle_t t)
{
    if (!s_connected) start_ap();
}

static void on_ap_off(TimerHandle_t t)
{
    stop_ap();
}

static void on_retry(TimerHandle_t t)
{
    if (!s_connected && !s_user_connect) esp_wifi_connect();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        wifi_config_t cfg;
        if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0]) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *e = data;
        bool was = s_connected;
        s_connected = false;
        // Our own esp_wifi_disconnect() reports ASSOC_LEAVE; that isn't a failure.
        if (e->reason != WIFI_REASON_ASSOC_LEAVE) {
            s_last_reason = e->reason;
            xEventGroupSetBits(s_events, FAILED_BIT);
        }
        if (was) ESP_LOGW(TAG, "lost Wi-Fi (reason %d)", e->reason);
        if (!s_user_connect) {
            // Keep retrying in the background; the setup AP opens if this drags on.
            xTimerStart(s_retry, 0);
            if (was) xTimerStart(s_fallback, 0);
        }
        show_status();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        snprintf(s_address, sizeof(s_address), IPSTR, IP2STR(&e->ip_info.ip));
        s_connected = true;
        xTimerStop(s_fallback, 0);
        xEventGroupClearBits(s_events, FAILED_BIT);
        xEventGroupSetBits(s_events, CONNECTED_BIT);
        ESP_LOGI(TAG, "connected, http://%s", s_address);
        if (s_ap_active) xTimerStart(s_ap_off, 0);
        show_status();
    }
}

esp_err_t wifi_start(void)
{
    s_events = xEventGroupCreate();
    s_fallback = xTimerCreate("wifi_fallback", pdMS_TO_TICKS(FALLBACK_MS), pdFALSE, NULL, on_fallback);
    s_ap_off = xTimerCreate("wifi_ap_off", pdMS_TO_TICKS(AP_GRACE_MS), pdFALSE, NULL, on_ap_off);
    s_retry = xTimerCreate("wifi_retry", pdMS_TO_TICKS(2000), pdFALSE, NULL, on_retry);

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    s_sta = esp_netif_create_default_wifi_sta();
    s_ap = esp_netif_create_default_wifi_ap();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_hostname, sizeof(s_hostname), "FanWave-%02X%02X", mac[4], mac[5]);
    esp_netif_set_hostname(s_sta, s_hostname);

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_FLASH), TAG, "storage");
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);

    wifi_config_t saved;
    bool have_network = esp_wifi_get_config(WIFI_IF_STA, &saved) == ESP_OK && saved.sta.ssid[0];
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    if (have_network) {
        ESP_LOGI(TAG, "joining \"%s\"", (char *)saved.sta.ssid);
        xTimerStart(s_fallback, 0);
    } else {
        start_ap();
    }
    show_status();
    return ESP_OK;
}

static int by_rssi(const void *a, const void *b)
{
    return ((const wifi_ap_record_t *)b)->rssi - ((const wifi_ap_record_t *)a)->rssi;
}

cJSON *wifi_scan_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    if (esp_wifi_scan_start(NULL, true) != ESP_OK) return arr;
    uint16_t n = MAX_SCAN;
    wifi_ap_record_t *recs = calloc(n, sizeof(*recs));
    if (!recs || esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) n = 0;
    qsort(recs, n, sizeof(*recs), by_rssi);
    for (uint16_t i = 0; i < n; i++) {
        if (!recs[i].ssid[0]) continue; // hidden
        bool dup = false;
        for (uint16_t j = 0; j < i && !dup; j++) dup = strcmp((char *)recs[j].ssid, (char *)recs[i].ssid) == 0;
        if (dup) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", (char *)recs[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", recs[i].rssi);
        cJSON_AddBoolToObject(o, "secured", recs[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(arr, o);
    }
    free(recs);
    return arr;
}

static const char *reason_text(wifi_err_reason_t r)
{
    switch (r) {
    case WIFI_REASON_NO_AP_FOUND:
        return "Network not found.";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return "Wrong password.";
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
        return "The router refused the connection.";
    default:
        return "Couldn't connect.";
    }
}

esp_err_t wifi_connect(const char *ssid, const char *password, char *address, char *err)
{
    if (!ssid || !*ssid || strlen(ssid) > 32 || (password && strlen(password) > 63)) {
        strcpy(err, "Invalid network name or password.");
        return ESP_ERR_INVALID_ARG;
    }
    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    if (password) strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = password && *password ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    wifi_config_t previous = {0};
    esp_wifi_get_config(WIFI_IF_STA, &previous);

    s_user_connect = true;
    xTimerStop(s_ap_off, 0);
    xTimerStop(s_retry, 0);
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));
    xEventGroupClearBits(s_events, CONNECTED_BIT | FAILED_BIT);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_connect();
    EventBits_t bits = xEventGroupWaitBits(s_events, CONNECTED_BIT | FAILED_BIT, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));
    s_user_connect = false;
    if (bits & CONNECTED_BIT) {
        strlcpy(address, s_address, 64);
        return ESP_OK;
    }
    strcpy(err, bits & FAILED_BIT ? reason_text(s_last_reason) : "Timed out joining the network.");
    // Go back to the network that worked before, if any, so a typo doesn't
    // take the hub offline; otherwise stay in setup.
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &previous);
    if (previous.sta.ssid[0]) esp_wifi_connect();
    start_ap();
    return ESP_FAIL;
}

wifi_status_t wifi_status(void)
{
    wifi_status_t s = {.connected = s_connected, .ap_active = s_ap_active, .rssi = 0};
    strlcpy(s.hostname, s_hostname, sizeof(s.hostname));
    if (s_connected) {
        strlcpy(s.address, s_address, sizeof(s.address));
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            strlcpy(s.ssid, (char *)ap.ssid, sizeof(s.ssid));
            s.rssi = ap.rssi;
        }
    } else if (s_ap_active) {
        strlcpy(s.address, "192.168.4.1", sizeof(s.address));
    }
    return s;
}
