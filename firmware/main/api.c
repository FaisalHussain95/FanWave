// HTTP API (docs/api.md), the /api/events WebSocket, and the web app's files.

#include "api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "captures.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "fans.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hub.h"
#include "rf.h"
#include "settings.h"
#include "status_led.h"
#include "storage.h"
#include "wifi.h"

static const char *TAG = "api";

#define MAX_BODY (64 * 1024)
#define MAX_RESTORE (512 * 1024)
#define CHUNK 4096

static httpd_handle_t s_server;

// --- helpers --------------------------------------------------------------

static esp_err_t send_json(httpd_req_t *req, const char *status, cJSON *json)
{
    char now[24];
    snprintf(now, sizeof(now), "%lld", (long long)uptime_ms());
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Hub-Now", now);
    char *text = json ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    esp_err_t err = httpd_resp_sendstr(req, text ? text : "null");
    cJSON_free(text);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "error", message);
    return send_json(req, status, o);
}

static esp_err_t send_no_content(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/**
 * Refuse POSTs made from another site's page: browsers always send Origin on
 * those, and it won't match the hub's own address.
 */
static bool same_origin(httpd_req_t *req)
{
    char origin[96], host[80], expected[96];
    if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK) return true;
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) return false;
    snprintf(expected, sizeof(expected), "http://%s", host);
    return strcmp(origin, expected) == 0;
}

/** Read and parse a JSON body. Sends the error response itself and returns NULL on failure. */
static cJSON *read_json(httpd_req_t *req, size_t max)
{
    if (!same_origin(req)) {
        send_error(req, "403 Forbidden", "Cross-origin requests are not allowed.");
        return NULL;
    }
    if (req->content_len == 0 || req->content_len > max) {
        send_error(req, "413 Payload Too Large", req->content_len ? "Request too large." : "Missing body.");
        return NULL;
    }
    char *buf = heap_caps_malloc(req->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(req->content_len + 1);
    if (!buf) {
        send_error(req, "500 Internal Server Error", "Out of memory.");
        return NULL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, buf + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            free(buf);
            return NULL;
        }
        got += n;
    }
    buf[got] = '\0';
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    if (!json) send_error(req, "400 Bad Request", "Body is not valid JSON.");
    return json;
}

/** The {id} in /api/<collection>/{id}/<action>. */
static bool path_id(const char *uri, const char *prefix, const char *action, char *id, size_t size)
{
    size_t plen = strlen(prefix);
    if (strncmp(uri, prefix, plen) != 0) return false;
    const char *start = uri + plen, *slash = strchr(start, '/');
    if (!slash || slash == start || (size_t)(slash - start) >= size) return false;
    const char *end = strchr(slash, '?');
    size_t alen = end ? (size_t)(end - slash - 1) : strlen(slash + 1);
    if (alen != strlen(action) || strncmp(slash + 1, action, alen) != 0) return false;
    memcpy(id, start, slash - start);
    id[slash - start] = '\0';
    return true;
}

// --- hub ------------------------------------------------------------------

static cJSON *led_json(void)
{
    status_settings_t s = status_led_settings();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enabled", s.enabled);
    cJSON_AddNumberToObject(o, "brightness", s.brightness);
    cJSON_AddStringToObject(o, "idle", s.idle_dim ? "dim" : "off");
    cJSON_AddBoolToObject(o, "radioFlashes", s.radio_flashes);
    return o;
}

static esp_err_t get_hub(httpd_req_t *req)
{
    wifi_status_t w = wifi_status();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", w.hostname);
    cJSON_AddStringToObject(o, "hardware", "ESP32‑S3 · CC1101");
    cJSON_AddStringToObject(o, "host", w.address[0] ? w.address : "");
    cJSON_AddBoolToObject(o, "online", true);
    cJSON_AddNumberToObject(o, "uptimeSeconds", (double)(uptime_ms() / 1000));
    cJSON_AddNumberToObject(o, "wifiRssi", w.rssi);
    cJSON_AddNumberToObject(o, "heapFreeBytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    if (w.connected) cJSON_AddStringToObject(o, "wifiNetwork", w.ssid);
    else cJSON_AddNullToObject(o, "wifiNetwork");
    cJSON_AddBoolToObject(o, "setupMode", w.ap_active);
    cJSON_AddBoolToObject(o, "accessPointFallback", true);
    cJSON_AddBoolToObject(o, "localOnly", true);
    cJSON_AddBoolToObject(o, "radioOk", hub_radio_ok());
    cJSON_AddItemToObject(o, "led", led_json());
    cJSON_AddStringToObject(o, "firmware", esp_app_get_description()->version);
    cJSON_AddNullToObject(o, "update"); // no update server: firmware is uploaded to /api/ota
    return send_json(req, "200 OK", o);
}

static esp_err_t post_led(httpd_req_t *req)
{
    cJSON *body = read_json(req, 1024);
    if (!body) return ESP_OK;
    status_settings_t s = status_led_settings();
    const cJSON *v;
    if (cJSON_IsBool(v = cJSON_GetObjectItemCaseSensitive(body, "enabled"))) s.enabled = cJSON_IsTrue(v);
    if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(body, "brightness"))) {
        int b = (int)v->valuedouble;
        s.brightness = (uint8_t)(b < 0 ? 0 : b > 100 ? 100 : b);
    }
    if (cJSON_IsString(v = cJSON_GetObjectItemCaseSensitive(body, "idle"))) s.idle_dim = strcmp(v->valuestring, "off") != 0;
    if (cJSON_IsBool(v = cJSON_GetObjectItemCaseSensitive(body, "radioFlashes"))) s.radio_flashes = cJSON_IsTrue(v);
    cJSON_Delete(body);
    status_led_set_settings(&s);
    settings_save_led(&s);
    return send_json(req, "200 OK", led_json());
}

static esp_err_t post_identify(httpd_req_t *req)
{
    if (!same_origin(req)) return send_error(req, "403 Forbidden", "Cross-origin requests are not allowed.");
    status_led_identify(5000);
    return send_no_content(req);
}

static void restart_soon(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static esp_err_t post_ota(httpd_req_t *req)
{
    if (!same_origin(req)) return send_error(req, "403 Forbidden", "Cross-origin requests are not allowed.");
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) return send_error(req, "500 Internal Server Error", "No OTA partition.");
    if (req->content_len == 0 || req->content_len > part->size) {
        return send_error(req, "413 Payload Too Large", "Firmware image is empty or too large.");
    }
    esp_ota_handle_t ota;
    if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "Couldn't start the update.");
    }
    status_led_set_updating(true);
    char *buf = malloc(CHUNK);
    size_t left = req->content_len;
    esp_err_t err = buf ? ESP_OK : ESP_ERR_NO_MEM;
    while (err == ESP_OK && left) {
        int n = httpd_req_recv(req, buf, left < CHUNK ? left : CHUNK);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            err = ESP_FAIL;
            break;
        }
        err = esp_ota_write(ota, buf, n);
        left -= n;
    }
    free(buf);
    if (err == ESP_OK) err = esp_ota_end(ota);
    else esp_ota_abort(ota);
    status_led_set_updating(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        return send_error(req, "400 Bad Request",
                          err == ESP_ERR_OTA_VALIDATE_FAILED ? "That file is not valid FanWave firmware."
                                                             : "The update didn't finish. Try again.");
    }
    esp_app_desc_t desc;
    esp_ota_get_partition_description(part, &desc);
    if (esp_ota_set_boot_partition(part) != ESP_OK) return send_error(req, "500 Internal Server Error", "Couldn't switch to the new firmware.");
    status_led_flash(STATUS_FLASH_SUCCESS);
    ESP_LOGI(TAG, "firmware %s installed, restarting", desc.version);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "version", desc.version);
    send_json(req, "200 OK", o);
    xTaskCreate(restart_soon, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

// --- Wi-Fi ----------------------------------------------------------------

static esp_err_t get_scan(httpd_req_t *req)
{
    return send_json(req, "200 OK", wifi_scan_json());
}

static esp_err_t post_wifi(httpd_req_t *req)
{
    cJSON *body = read_json(req, 1024);
    if (!body) return ESP_OK;
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(body, "ssid");
    const cJSON *pass = cJSON_GetObjectItemCaseSensitive(body, "password");
    char address[64] = "", err[96] = "";
    esp_err_t e = wifi_connect(cJSON_IsString(ssid) ? ssid->valuestring : NULL,
                               cJSON_IsString(pass) ? pass->valuestring : "", address, err);
    cJSON_Delete(body);
    if (e != ESP_OK) return send_error(req, "400 Bad Request", err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "address", address);
    return send_json(req, "200 OK", o);
}

// --- rooms and fans -------------------------------------------------------

static esp_err_t get_rooms(httpd_req_t *req)
{
    return send_json(req, "200 OK", fans_rooms_json());
}

static esp_err_t post_rooms(httpd_req_t *req)
{
    cJSON *body = read_json(req, 1024);
    if (!body) return ESP_OK;
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(body, "name");
    char err[96] = "";
    cJSON *room = fans_add_room(cJSON_IsString(name) ? name->valuestring : NULL, err);
    cJSON_Delete(body);
    return room ? send_json(req, "200 OK", room) : send_error(req, "400 Bad Request", err);
}

static esp_err_t get_fans(httpd_req_t *req)
{
    return send_json(req, "200 OK", fans_list_json());
}

static esp_err_t post_fans(httpd_req_t *req)
{
    cJSON *body = read_json(req, MAX_BODY);
    if (!body) return ESP_OK;
    cc1101_config_t radio = hub_radio();
    char err[96] = "";
    cJSON *fan = fans_add(body, &radio, err);
    cJSON_Delete(body);
    return fan ? send_json(req, "200 OK", fan) : send_error(req, "400 Bad Request", err);
}

static esp_err_t post_fan_command(httpd_req_t *req)
{
    char id[48];
    if (!path_id(req->uri, "/api/fans/", "command", id, sizeof(id))) {
        return send_error(req, "404 Not Found", "No such endpoint.");
    }
    cJSON *body = read_json(req, 1024);
    if (!body) return ESP_OK;
    char err[96] = "";
    cJSON *fan = fans_command(id, body, err);
    cJSON_Delete(body);
    if (!fan) return send_error(req, strcmp(err, "Unknown fan.") == 0 ? "404 Not Found" : "400 Bad Request", err);
    api_broadcast_fan(cJSON_Duplicate(fan, true));
    return send_json(req, "200 OK", fan);
}

// --- radio ----------------------------------------------------------------

static esp_err_t get_captures(httpd_req_t *req)
{
    return send_json(req, "200 OK", captures_list_json());
}

static esp_err_t get_analysis(httpd_req_t *req)
{
    char id[16];
    if (!path_id(req->uri, "/api/captures/", "analysis", id, sizeof(id))) {
        return send_error(req, "404 Not Found", "No such endpoint.");
    }
    cJSON *arr = captures_matches_json((uint32_t)strtoul(id, NULL, 10));
    return arr ? send_json(req, "200 OK", arr) : send_error(req, "404 Not Found", "That capture has expired.");
}

static cJSON *receiver_json(void)
{
    cc1101_config_t r = hub_radio();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "listening", radio_listening());
    cJSON_AddStringToObject(o, "modulation", r.modulation == CC1101_MOD_OOK ? "OOK" : "FSK");
    cJSON_AddNumberToObject(o, "frequencyMHz", r.frequency_hz / 1e6);
    return o;
}

static esp_err_t get_receiver(httpd_req_t *req)
{
    return send_json(req, "200 OK", receiver_json());
}

static esp_err_t post_receiver(httpd_req_t *req)
{
    cJSON *body = read_json(req, 1024);
    if (!body) return ESP_OK;
    cc1101_config_t r = hub_radio();
    bool change = false;
    const cJSON *v;
    if (cJSON_IsString(v = cJSON_GetObjectItemCaseSensitive(body, "modulation"))) {
        cc1101_modulation_t m = strcmp(v->valuestring, "FSK") == 0 ? CC1101_MOD_2FSK : CC1101_MOD_OOK;
        change |= m != r.modulation;
        r.modulation = m;
    }
    if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(body, "frequencyMHz"))) {
        uint32_t hz = (uint32_t)(v->valuedouble * 1e6 + 0.5);
        if (!cc1101_frequency_supported(hz)) {
            cJSON_Delete(body);
            return send_error(req, "400 Bad Request", "The CC1101 covers 300–348, 387–464 and 779–928 MHz.");
        }
        change |= hz != r.frequency_hz;
        r.frequency_hz = hz;
    }
    const cJSON *listening = cJSON_GetObjectItemCaseSensitive(body, "listening");
    esp_err_t err = change ? hub_set_radio(&r) : ESP_OK;
    if (err == ESP_OK && cJSON_IsBool(listening)) err = radio_set_listening(cJSON_IsTrue(listening));
    cJSON_Delete(body);
    if (err != ESP_OK) return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    return send_json(req, "200 OK", receiver_json());
}

// --- backup ---------------------------------------------------------------

static esp_err_t get_backup(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"fanwave-backup.json\"");
    return send_json(req, "200 OK", fans_backup_json());
}

static esp_err_t post_restore(httpd_req_t *req)
{
    cJSON *body = read_json(req, MAX_RESTORE);
    if (!body) return ESP_OK;
    char err[96] = "";
    esp_err_t e = fans_restore(body, err);
    cJSON_Delete(body);
    return e == ESP_OK ? send_no_content(req) : send_error(req, "400 Bad Request", err);
}

static esp_err_t not_found(httpd_req_t *req)
{
    return send_error(req, "404 Not Found", "No such endpoint.");
}

// --- events ---------------------------------------------------------------

static esp_err_t ws_events(httpd_req_t *req)
{
    if (req->method == HTTP_GET) return ESP_OK; // handshake
    // Clients don't send anything meaningful; read and drop it.
    httpd_ws_frame_t frame = {0};
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK || frame.len == 0) return err;
    uint8_t *buf = malloc(frame.len);
    if (!buf) return ESP_ERR_NO_MEM;
    frame.payload = buf;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    free(buf);
    return err;
}

static void broadcast_work(void *arg)
{
    char *text = arg;
    size_t n = CONFIG_LWIP_MAX_SOCKETS;
    int fds[CONFIG_LWIP_MAX_SOCKETS];
    if (httpd_get_client_list(s_server, &n, fds) == ESP_OK) {
        httpd_ws_frame_t frame = {.type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)text, .len = strlen(text)};
        for (size_t i = 0; i < n; i++) {
            if (httpd_ws_get_fd_info(s_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                httpd_ws_send_frame_async(s_server, fds[i], &frame);
            }
        }
    }
    cJSON_free(text);
}

void api_broadcast(cJSON *message)
{
    if (!message) return;
    char *text = s_server ? cJSON_PrintUnformatted(message) : NULL;
    cJSON_Delete(message);
    if (text && httpd_queue_work(s_server, broadcast_work, text) != ESP_OK) cJSON_free(text);
}

void api_broadcast_fan(cJSON *fan)
{
    if (!fan) return;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "fan");
    cJSON_AddItemToObject(o, "fan", fan);
    api_broadcast(o);
}

void api_broadcast_capture(const capture_t *c)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "capture");
    cJSON_AddNumberToObject(o, "now", (double)uptime_ms());
    cJSON_AddItemToObject(o, "capture", capture_json(c));
    cJSON *matches = cJSON_AddArrayToObject(o, "matches");
    for (size_t i = 0; i < c->match_count; i++) cJSON_AddItemToArray(matches, match_json(&c->matches[i]));
    api_broadcast(o);
}

// --- web app files ----------------------------------------------------------

static const char *content_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    static const char *types[][2] = {
        {".html", "text/html; charset=utf-8"}, {".js", "text/javascript"}, {".css", "text/css"},
        {".svg", "image/svg+xml"}, {".woff2", "font/woff2"}, {".json", "application/json"},
        {".webmanifest", "application/manifest+json"}, {".png", "image/png"}, {".ico", "image/x-icon"},
    };
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        if (strcmp(ext, types[i][0]) == 0) return types[i][1];
    }
    return "application/octet-stream";
}

static esp_err_t get_file(httpd_req_t *req)
{
    char path[128];
    const char *uri = req->uri;
    size_t len = strcspn(uri, "?#");
    if (len == 1 && uri[0] == '/') {
        snprintf(path, sizeof(path), WWW_ROOT "/index.html");
    } else if (len + sizeof(WWW_ROOT) < sizeof(path) && !strstr(uri, "..")) {
        snprintf(path, sizeof(path), WWW_ROOT "%.*s", (int)len, uri);
    } else {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        if (strcmp(path, WWW_ROOT "/index.html") == 0) {
            return httpd_resp_sendstr(req, "FanWave is running, but the web app isn't in flash. "
                                           "Build web/ and flash the firmware again.");
        }
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    }
    httpd_resp_set_type(req, content_type(path));
    // Hashed asset names never change content; everything else must revalidate.
    httpd_resp_set_hdr(req, "Cache-Control",
                       strncmp(path, WWW_ROOT "/assets/", sizeof(WWW_ROOT "/assets/") - 1) == 0
                           ? "public, max-age=31536000, immutable"
                           : "no-cache");
    char *buf = malloc(CHUNK);
    esp_err_t err = buf ? ESP_OK : ESP_ERR_NO_MEM;
    size_t n;
    while (err == ESP_OK && (n = fread(buf, 1, CHUNK, f)) > 0) err = httpd_resp_send_chunk(req, buf, n);
    fclose(f);
    free(buf);
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, NULL, 0);
    return err;
}

// --- start ----------------------------------------------------------------

esp_err_t api_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 32;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    cfg.max_open_sockets = 7;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &cfg), TAG, "start");

    const httpd_uri_t routes[] = {
        {.uri = "/api/hub", .method = HTTP_GET, .handler = get_hub},
        {.uri = "/api/led", .method = HTTP_POST, .handler = post_led},
        {.uri = "/api/identify", .method = HTTP_POST, .handler = post_identify},
        {.uri = "/api/ota", .method = HTTP_POST, .handler = post_ota},
        {.uri = "/api/wifi/scan", .method = HTTP_GET, .handler = get_scan},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = post_wifi},
        {.uri = "/api/rooms", .method = HTTP_GET, .handler = get_rooms},
        {.uri = "/api/rooms", .method = HTTP_POST, .handler = post_rooms},
        {.uri = "/api/fans", .method = HTTP_GET, .handler = get_fans},
        {.uri = "/api/fans", .method = HTTP_POST, .handler = post_fans},
        {.uri = "/api/fans/*", .method = HTTP_POST, .handler = post_fan_command},
        {.uri = "/api/captures", .method = HTTP_GET, .handler = get_captures},
        {.uri = "/api/captures/*", .method = HTTP_GET, .handler = get_analysis},
        {.uri = "/api/receiver", .method = HTTP_GET, .handler = get_receiver},
        {.uri = "/api/receiver", .method = HTTP_POST, .handler = post_receiver},
        {.uri = "/api/backup", .method = HTTP_GET, .handler = get_backup},
        {.uri = "/api/restore", .method = HTTP_POST, .handler = post_restore},
        {.uri = "/api/events", .method = HTTP_GET, .handler = ws_events, .is_websocket = true},
        {.uri = "/api/*", .method = HTTP_GET, .handler = not_found},
        {.uri = "/api/*", .method = HTTP_POST, .handler = not_found},
        {.uri = "/*", .method = HTTP_GET, .handler = get_file},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &routes[i]), TAG, "route %s", routes[i].uri);
    }
    ESP_LOGI(TAG, "HTTP API on port %d", cfg.server_port);
    return ESP_OK;
}
