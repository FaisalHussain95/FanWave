#include "fans.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "fw_protocol.h"
#include "hub.h"
#include "storage.h"

static const char *TAG = "fans";

#define ROOMS_PATH DATA_ROOT "/rooms.json"
#define FANS_PATH DATA_ROOT "/fans.json"
#define MAX_NAME 40
#define DECODED_REPEATS 8
#define MIN_CONFIDENCE 0.5f
#define PAIR_SENDS 3     // a pairing press is sent this many times
#define PAIR_GAP_MS 300

static SemaphoreHandle_t s_lock;
static cJSON *s_rooms;
static cJSON *s_fans;

// --- small JSON helpers ---------------------------------------------------

static const char *jstr(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static bool jnum(const cJSON *o, const char *key, double *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v)) return false;
    *out = v->valuedouble;
    return true;
}

static void jset(cJSON *o, const char *key, cJSON *value)
{
    if (cJSON_HasObjectItem(o, key)) cJSON_ReplaceItemInObjectCaseSensitive(o, key, value);
    else cJSON_AddItemToObject(o, key, value);
}

static cJSON *find_by_id(cJSON *array, const char *id)
{
    cJSON *item;
    cJSON_ArrayForEach(item, array) {
        const char *v = jstr(item, "id");
        if (v && strcmp(v, id) == 0) return item;
    }
    return NULL;
}

/** Lower-case, dashes for anything else, unique within the array. */
static void make_id(cJSON *array, const char *name, char *out, size_t size)
{
    char base[32];
    size_t w = 0;
    bool dash = false;
    for (const char *p = name; *p && w < sizeof(base) - 1; p++) {
        if (isalnum((unsigned char)*p)) {
            if (dash && w) base[w++] = '-';
            base[w++] = (char)tolower((unsigned char)*p);
            dash = false;
        } else {
            dash = true;
        }
    }
    base[w] = '\0';
    if (!w) strcpy(base, "item");
    snprintf(out, size, "%s", base);
    for (int n = 2; find_by_id(array, out); n++) snprintf(out, size, "%s-%d", base, n);
}

static bool valid_name(const char *name)
{
    if (!name) return false;
    size_t len = strlen(name);
    return len > 0 && len <= MAX_NAME;
}

static void raw_path(const char *file, char *out, size_t size)
{
    snprintf(out, size, DATA_ROOT "/%s", file);
}

static void save_rooms(void)
{
    if (storage_write_json(ROOMS_PATH, s_rooms) != ESP_OK) ESP_LOGE(TAG, "couldn't save rooms");
}

static void save_fans(void)
{
    if (storage_write_json(FANS_PATH, s_fans) != ESP_OK) ESP_LOGE(TAG, "couldn't save fans");
}

/**
 * The remote behind a fan's decoded buttons: its protocol and address, when
 * every button decodes and they all come from the same remote. That is what
 * lets a copy send the same buttons under another address.
 */
static bool fan_remote(const cJSON *fan, const fw_protocol_t **proto, uint64_t *address)
{
    const cJSON *commands = cJSON_GetObjectItemCaseSensitive(fan, "commands"), *cmd;
    bool have = false;
    cJSON_ArrayForEach(cmd, commands) {
        const char *id = jstr(cmd, "protocol"), *b = jstr(cmd, "bits");
        const fw_protocol_t *p = id ? fw_protocol_find(id) : NULL;
        uint64_t a;
        if (!p || !b || !fw_frame_address(p, strtoull(b, NULL, 2), (uint8_t)strlen(b), &a)) return false;
        if (have && (p != *proto || a != *address)) return false;
        *proto = p;
        *address = a;
        have = true;
    }
    return have;
}

static void format_address(const fw_protocol_t *p, uint64_t address, char *out, size_t size)
{
    snprintf(out, size, "0x%0*llX", (p->address_bits + 3) / 4, (unsigned long long)address);
}

/**
 * Fields derived from the learned buttons, for the app: "address" (null for
 * raw or mixed buttons), "copyable" (a copy with a new address can be made)
 * and "pairCode" (a pair button was learned).
 */
static void annotate(cJSON *fan)
{
    const fw_protocol_t *p;
    uint64_t address;
    bool known = fan_remote(fan, &p, &address);
    char text[24];
    if (known) format_address(p, address, text, sizeof(text));
    jset(fan, "address", known ? cJSON_CreateString(text) : cJSON_CreateNull());
    jset(fan, "copyable", cJSON_CreateBool(known));
    jset(fan, "pairCode", cJSON_CreateBool(cJSON_HasObjectItem(cJSON_GetObjectItemCaseSensitive(fan, "commands"), "pair")));
}

// --- rooms ----------------------------------------------------------------

esp_err_t fans_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_rooms = storage_read_json(ROOMS_PATH);
    if (!cJSON_IsArray(s_rooms)) {
        cJSON_Delete(s_rooms);
        // Pairing needs a room to put the fan in.
        s_rooms = cJSON_Parse("[{\"id\":\"living-room\",\"name\":\"Living room\"},{\"id\":\"bedroom\",\"name\":\"Bedroom\"}]");
        save_rooms();
    }
    s_fans = storage_read_json(FANS_PATH);
    if (!cJSON_IsArray(s_fans)) {
        cJSON_Delete(s_fans);
        s_fans = cJSON_CreateArray();
    }
    // What the hub heard from remotes before a reboot is no longer known.
    cJSON *fan;
    cJSON_ArrayForEach(fan, s_fans) {
        jset(fan, "confirmed", cJSON_CreateNull());
        annotate(fan); // fans saved by older firmware lack the derived fields
    }
    ESP_LOGI(TAG, "%d rooms, %d fans", cJSON_GetArraySize(s_rooms), cJSON_GetArraySize(s_fans));
    return ESP_OK;
}

cJSON *fans_rooms_json(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON *copy = cJSON_Duplicate(s_rooms, true);
    xSemaphoreGive(s_lock);
    return copy;
}

cJSON *fans_list_json(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON *copy = cJSON_Duplicate(s_fans, true);
    xSemaphoreGive(s_lock);
    return copy;
}

cJSON *fans_add_room(const char *name, char *err)
{
    if (!valid_name(name)) {
        strcpy(err, "Room names are 1-40 characters.");
        return NULL;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    char id[48];
    make_id(s_rooms, name, id, sizeof(id));
    cJSON *room = cJSON_CreateObject();
    cJSON_AddStringToObject(room, "id", id);
    cJSON_AddStringToObject(room, "name", name);
    cJSON_AddItemToArray(s_rooms, room);
    save_rooms();
    cJSON *copy = cJSON_Duplicate(room, true);
    xSemaphoreGive(s_lock);
    return copy;
}

// --- pairing --------------------------------------------------------------

static cJSON *default_state(void)
{
    return cJSON_Parse("{\"power\":false,\"speed\":1,\"light\":false,\"reverse\":false,\"timer\":false}");
}

/** Store one learned button from a capture: decoded when a decoder is confident, raw otherwise. */
static cJSON *learn_command(const char *fan_id, const char *key, const capture_t *c, const fw_protocol_t **proto)
{
    cJSON *cmd = cJSON_CreateObject();
    const fw_match_t *m = c->match_count ? &c->matches[0] : NULL;
    if (m && m->confidence >= MIN_CONFIDENCE) {
        char bits[65];
        bits_to_string(m->bits, m->bit_count, bits);
        cJSON_AddStringToObject(cmd, "protocol", m->protocol->id);
        cJSON_AddStringToObject(cmd, "bits", bits);
        cJSON_AddNumberToObject(cmd, "baseUs", m->base_us);
        if (!*proto) *proto = m->protocol;
        return cmd;
    }
    char file[64], path[96];
    snprintf(file, sizeof(file), "raw-%s-%s.bin", fan_id, key);
    raw_path(file, path, sizeof(path));
    if (storage_write_durations(path, c->durations, c->count) != ESP_OK) {
        cJSON_Delete(cmd);
        return NULL;
    }
    cJSON_AddStringToObject(cmd, "raw", file);
    return cmd;
}

cJSON *fans_add(const cJSON *body, const cc1101_config_t *radio, char *err)
{
    const char *name = jstr(body, "name"), *room_id = jstr(body, "roomId"), *kind = jstr(body, "kind");
    const char *address = jstr(body, "address");
    const cJSON *slots = cJSON_GetObjectItemCaseSensitive(body, "slots");
    if (!valid_name(name)) {
        strcpy(err, "Fan names are 1-40 characters.");
        return NULL;
    }
    if (!cJSON_IsArray(slots)) {
        strcpy(err, "Missing learned buttons.");
        return NULL;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON *result = NULL;
    if (!room_id || !find_by_id(s_rooms, room_id)) {
        strcpy(err, "Unknown room.");
        goto done;
    }

    char id[48];
    make_id(s_fans, name, id, sizeof(id));
    cJSON *commands = cJSON_CreateObject();
    const fw_protocol_t *proto = NULL;
    int learned = 0, speeds = 0;
    bool light = false, reverse = false, timer = false;

    const cJSON *slot;
    cJSON_ArrayForEach(slot, slots) {
        const char *key = jstr(slot, "key"), *status = jstr(slot, "status");
        double capture_id;
        if (!key || !status || strcmp(status, "done") != 0 || !jnum(slot, "captureId", &capture_id)) continue;
        if (strlen(key) > 12 || strchr(key, '/')) continue;
        capture_t c;
        if (!captures_get((uint32_t)capture_id, &c)) {
            snprintf(err, 96, "The capture for \"%s\" has expired. Learn it again.", key);
            cJSON_Delete(commands);
            goto done;
        }
        cJSON *cmd = learn_command(id, key, &c, &proto);
        captures_free(&c);
        if (!cmd) {
            strcpy(err, "Couldn't save a learned code. Is the flash full?");
            cJSON_Delete(commands);
            goto done;
        }
        cJSON_AddItemToObject(commands, key, cmd);
        learned++;
        int n;
        if (sscanf(key, "speed-%d", &n) == 1 && n > speeds) speeds = n;
        light |= strcmp(key, "light") == 0;
        reverse |= strcmp(key, "reverse") == 0;
        timer |= strcmp(key, "timer") == 0;
    }
    if (!learned) {
        strcpy(err, "Learn at least one button.");
        cJSON_Delete(commands);
        goto done;
    }

    cJSON *fan = cJSON_CreateObject();
    cJSON_AddStringToObject(fan, "id", id);
    cJSON_AddStringToObject(fan, "name", name);
    cJSON_AddStringToObject(fan, "roomId", room_id);
    cJSON_AddStringToObject(fan, "kind", kind && strlen(kind) <= MAX_NAME ? kind : "Fan");
    if (proto) cJSON_AddStringToObject(fan, "protocol", proto->id);
    else cJSON_AddNullToObject(fan, "protocol");
    char label[80];
    if (proto) snprintf(label, sizeof(label), "%s%s%s", proto->name, address ? " · " : "", address ? address : "");
    else snprintf(label, sizeof(label), "Raw · %d code%s", learned, learned == 1 ? "" : "s");
    cJSON_AddStringToObject(fan, "protocolLabel", label);
    cJSON_AddNumberToObject(fan, "frequencyMHz", radio->frequency_hz / 1e6);
    cJSON_AddStringToObject(fan, "modulation", radio->modulation == CC1101_MOD_OOK ? "OOK" : "FSK");
    cJSON_AddNumberToObject(fan, "speeds", speeds ? speeds : 1);
    cJSON *caps = cJSON_AddArrayToObject(fan, "capabilities");
    cJSON_AddItemToArray(caps, cJSON_CreateString("power"));
    cJSON_AddItemToArray(caps, cJSON_CreateString("speed"));
    if (light) cJSON_AddItemToArray(caps, cJSON_CreateString("light"));
    if (reverse) cJSON_AddItemToArray(caps, cJSON_CreateString("reverse"));
    if (timer) cJSON_AddItemToArray(caps, cJSON_CreateString("timer"));
    cJSON_AddItemToObject(fan, "commanded", default_state());
    cJSON_AddNullToObject(fan, "confirmed");
    cJSON_AddNullToObject(fan, "lastSentAt");
    cJSON_AddItemToObject(fan, "commands", commands);
    annotate(fan);

    cJSON_AddItemToArray(s_fans, fan);
    save_fans();
    result = cJSON_Duplicate(fan, true);
    ESP_LOGI(TAG, "added fan %s with %d buttons", id, learned);
done:
    xSemaphoreGive(s_lock);
    return result;
}

// --- commands ---------------------------------------------------------------

static cc1101_config_t fan_radio(const cJSON *fan)
{
    cc1101_config_t r = hub_radio();
    double mhz;
    if (jnum(fan, "frequencyMHz", &mhz)) r.frequency_hz = (uint32_t)(mhz * 1e6 + 0.5);
    const char *mod = jstr(fan, "modulation");
    if (mod) r.modulation = strcmp(mod, "FSK") == 0 ? CC1101_MOD_2FSK : CC1101_MOD_OOK;
    return r;
}

static esp_err_t send_button(const cJSON *fan, const char *key, char *err)
{
    const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(fan, "commands"), key);
    if (!cmd) {
        snprintf(err, 96, "This fan has no learned \"%s\" button.", key);
        return ESP_ERR_NOT_FOUND;
    }
    uint32_t *d = NULL;
    size_t n = 0;
    const char *raw = jstr(cmd, "raw");
    if (raw) {
        char path[96];
        raw_path(raw, path, sizeof(path));
        n = storage_read_durations(path, &d);
    } else {
        const fw_protocol_t *p = fw_protocol_find(jstr(cmd, "protocol"));
        const char *bits = jstr(cmd, "bits");
        double base = 0;
        jnum(cmd, "baseUs", &base);
        if (p && bits && strlen(bits) >= 1 && strlen(bits) <= 64) {
            uint8_t count = (uint8_t)strlen(bits);
            size_t max = (size_t)DECODED_REPEATS * (count + 1) * 2;
            d = malloc(max * sizeof(uint32_t));
            if (d) n = fw_encode(p, strtoull(bits, NULL, 2), count, DECODED_REPEATS, (uint16_t)base, d, max);
        }
    }
    if (!n) {
        free(d);
        snprintf(err, 96, "The stored \"%s\" code is unreadable.", key);
        return ESP_FAIL;
    }
    cc1101_config_t radio = fan_radio(fan);
    esp_err_t e = hub_transmit_on(&radio, d, n);
    free(d);
    if (e != ESP_OK && hub_scanning()) snprintf(err, 96, "A frequency scan is running; try again when it finishes.");
    else if (e != ESP_OK) snprintf(err, 96, "Transmit failed: %s", esp_err_to_name(e));
    return e;
}

static bool has_command(const cJSON *fan, const char *key)
{
    return cJSON_HasObjectItem(cJSON_GetObjectItemCaseSensitive(fan, "commands"), key);
}

cJSON *fans_command(const char *id, const cJSON *patch, char *err)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON *result = NULL;
    cJSON *fan = find_by_id(s_fans, id);
    if (!fan) {
        strcpy(err, "Unknown fan.");
        goto done;
    }
    cJSON *commanded = cJSON_GetObjectItemCaseSensitive(fan, "commanded");
    double speeds = 1, cur_speed = 1, speed;
    jnum(fan, "speeds", &speeds);
    jnum(commanded, "speed", &cur_speed);
    bool on = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(commanded, "power"));
    const cJSON *power = cJSON_GetObjectItemCaseSensitive(patch, "power");
    bool has_speed = jnum(patch, "speed", &speed);
    if (has_speed && (speed < 1 || speed > speeds || speed != (int)speed)) {
        snprintf(err, 96, "Speed must be 1-%d.", (int)speeds);
        goto done;
    }

    // Work out which buttons to press, in order (see docs/api.md).
    const char *keys[4];
    char speed_key[16];
    int nkeys = 0;
    if (has_speed && !cJSON_IsFalse(power)) {
        snprintf(speed_key, sizeof(speed_key), "speed-%d", (int)speed);
        keys[nkeys++] = has_command(fan, speed_key) ? speed_key : "power";
    } else if (cJSON_IsTrue(power) && !on) {
        snprintf(speed_key, sizeof(speed_key), "speed-%d", (int)cur_speed);
        keys[nkeys++] = has_command(fan, speed_key) ? speed_key : "power";
    } else if (cJSON_IsFalse(power) && on) {
        keys[nkeys++] = "power";
    }
    static const char *toggles[] = {"light", "reverse", "timer"};
    for (int i = 0; i < 3; i++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(patch, toggles[i]);
        if (cJSON_IsBool(v) && cJSON_IsTrue(v) != cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(commanded, toggles[i]))) {
            keys[nkeys++] = toggles[i];
        }
    }

    for (int i = 0; i < nkeys; i++) {
        if (i) vTaskDelay(pdMS_TO_TICKS(150));
        if (send_button(fan, keys[i], err) != ESP_OK) goto done;
    }

    // Record what was sent.
    if (cJSON_IsBool(power)) jset(commanded, "power", cJSON_CreateBool(cJSON_IsTrue(power)));
    else if (has_speed) jset(commanded, "power", cJSON_CreateTrue());
    if (has_speed) jset(commanded, "speed", cJSON_CreateNumber(speed));
    for (int i = 0; i < 3; i++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(patch, toggles[i]);
        if (cJSON_IsBool(v)) jset(commanded, toggles[i], cJSON_CreateBool(cJSON_IsTrue(v)));
    }
    if (nkeys) jset(fan, "lastSentAt", cJSON_CreateNumber((double)uptime_ms()));
    save_fans();
    result = cJSON_Duplicate(fan, true);
done:
    xSemaphoreGive(s_lock);
    return result;
}

// --- virtual remotes ------------------------------------------------------------

static bool address_taken(const fw_protocol_t *p, uint64_t address)
{
    cJSON *fan;
    cJSON_ArrayForEach(fan, s_fans) {
        const fw_protocol_t *fp;
        uint64_t a;
        if (fan_remote(fan, &fp, &a) && fp == p && a == address) return true;
    }
    return false;
}

/** A random address no stored fan of this protocol uses, avoiding all zeros and all ones. */
static bool fresh_address(const fw_protocol_t *p, uint64_t *out)
{
    uint64_t top = p->address_bits >= 64 ? UINT64_MAX : (1ULL << p->address_bits) - 1;
    for (int tries = 0; tries < 64; tries++) {
        uint64_t a = (((uint64_t)esp_random() << 32) | esp_random()) & top;
        if (a == 0 || a == top || address_taken(p, a)) continue;
        *out = a;
        return true;
    }
    return false;
}

cJSON *fans_duplicate(const char *id, const cJSON *body, char *err)
{
    const char *name = jstr(body, "name"), *room_id = jstr(body, "roomId"), *wanted = jstr(body, "address");
    if (!valid_name(name)) {
        strcpy(err, "Fan names are 1-40 characters.");
        return NULL;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON *result = NULL;
    cJSON *src = find_by_id(s_fans, id);
    const fw_protocol_t *p;
    uint64_t old, address;
    if (!src) {
        strcpy(err, "Unknown fan.");
        goto done;
    }
    if (!room_id || !find_by_id(s_rooms, room_id)) {
        strcpy(err, "Unknown room.");
        goto done;
    }
    if (!fan_remote(src, &p, &old)) {
        strcpy(err, "Only remotes whose buttons all decode, from one remote, can be copied.");
        goto done;
    }
    if (wanted && *wanted) {
        char *end;
        address = strtoull(wanted, &end, 16);
        uint64_t top = p->address_bits >= 64 ? UINT64_MAX : (1ULL << p->address_bits) - 1;
        if (*end || address > top) {
            snprintf(err, 96, "The address must be hex, at most %d bits (0x0 to 0x%llX).", p->address_bits,
                     (unsigned long long)top);
            goto done;
        }
        if (address_taken(p, address)) {
            strcpy(err, "Another fan already uses that address.");
            goto done;
        }
    } else if (!fresh_address(p, &address)) {
        strcpy(err, "Couldn't find an unused address.");
        goto done;
    }

    cJSON *fan = cJSON_Duplicate(src, true);
    char new_id[48], text[24], label[80];
    make_id(s_fans, name, new_id, sizeof(new_id));
    jset(fan, "id", cJSON_CreateString(new_id));
    jset(fan, "name", cJSON_CreateString(name));
    jset(fan, "roomId", cJSON_CreateString(room_id));
    jset(fan, "copyOf", cJSON_CreateString(jstr(src, "name") ? jstr(src, "name") : ""));
    format_address(p, address, text, sizeof(text));
    snprintf(label, sizeof(label), "%s · %s", p->name, text);
    jset(fan, "protocolLabel", cJSON_CreateString(label));
    jset(fan, "commanded", default_state());
    jset(fan, "confirmed", cJSON_CreateNull());
    jset(fan, "lastSentAt", cJSON_CreateNull());
    cJSON *cmd;
    cJSON_ArrayForEach(cmd, cJSON_GetObjectItemCaseSensitive(fan, "commands")) {
        const char *b = jstr(cmd, "bits");
        uint8_t count = (uint8_t)strlen(b);
        uint64_t moved;
        fw_frame_with_address(p, strtoull(b, NULL, 2), count, address, &moved); // fan_remote() checked it fits
        char bits[65];
        bits_to_string(moved, count, bits);
        jset(cmd, "bits", cJSON_CreateString(bits));
    }
    annotate(fan);
    cJSON_AddItemToArray(s_fans, fan);
    save_fans();
    result = cJSON_Duplicate(fan, true);
    ESP_LOGI(TAG, "copied %s as %s, address %s", id, new_id, text);
done:
    xSemaphoreGive(s_lock);
    return result;
}

const char *fans_pair(const char *id, char *err)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const char *sent = NULL;
    cJSON *fan = find_by_id(s_fans, id);
    if (!fan) {
        strcpy(err, "Unknown fan.");
        goto done;
    }
    // Most learning receivers take any button while they're in pairing mode,
    // so without a learned pair button, power is the next best thing.
    const char *key = has_command(fan, "pair") ? "pair" : "power";
    for (int i = 0; i < PAIR_SENDS; i++) {
        if (i) vTaskDelay(pdMS_TO_TICKS(PAIR_GAP_MS));
        if (send_button(fan, key, err) != ESP_OK) goto done;
    }
    sent = key;
done:
    xSemaphoreGive(s_lock);
    return sent;
}

esp_err_t fans_remove(const char *id, char *err)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t e = ESP_ERR_NOT_FOUND;
    int index = 0;
    cJSON *fan;
    cJSON_ArrayForEach(fan, s_fans) {
        const char *fid = jstr(fan, "id");
        if (fid && strcmp(fid, id) == 0) break;
        index++;
    }
    if (!fan) {
        strcpy(err, "Unknown fan.");
        goto done;
    }
    cJSON *cmd;
    cJSON_ArrayForEach(cmd, cJSON_GetObjectItemCaseSensitive(fan, "commands")) {
        const char *raw = jstr(cmd, "raw");
        if (!raw) continue;
        char path[96];
        raw_path(raw, path, sizeof(path));
        remove(path);
    }
    cJSON_DeleteItemFromArray(s_fans, index);
    save_fans();
    ESP_LOGI(TAG, "removed fan %s", id);
    e = ESP_OK;
done:
    xSemaphoreGive(s_lock);
    return e;
}

// --- remotes heard over the air -------------------------------------------

cJSON *fans_observe(const capture_t *c)
{
    const fw_match_t *m = c->match_count ? &c->matches[0] : NULL;
    if (!m || m->confidence < MIN_CONFIDENCE) return NULL;
    char bits[65];
    bits_to_string(m->bits, m->bit_count, bits);

    cJSON *result = NULL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON *fan;
    cJSON_ArrayForEach(fan, s_fans) {
        cJSON *cmd;
        cJSON_ArrayForEach(cmd, cJSON_GetObjectItemCaseSensitive(fan, "commands")) {
            const char *p = jstr(cmd, "protocol"), *b = jstr(cmd, "bits");
            if (!p || !b || strcmp(p, m->protocol->id) || strcmp(b, bits)) continue;

            cJSON *confirmed = cJSON_GetObjectItemCaseSensitive(fan, "confirmed");
            if (!cJSON_IsObject(confirmed)) {
                confirmed = cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(fan, "commanded"), true);
                jset(fan, "confirmed", confirmed);
            }
            int n;
            const char *key = cmd->string;
            if (sscanf(key, "speed-%d", &n) == 1) {
                jset(confirmed, "power", cJSON_CreateTrue());
                jset(confirmed, "speed", cJSON_CreateNumber(n));
            } else if (strcmp(key, "power") == 0) {
                // Most fan remotes' power button toggles.
                bool was = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(confirmed, "power"));
                jset(confirmed, "power", cJSON_CreateBool(!was));
            } else if (cJSON_HasObjectItem(confirmed, key)) {
                bool was = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(confirmed, key));
                jset(confirmed, key, cJSON_CreateBool(!was));
            }
            result = cJSON_Duplicate(fan, true);
            goto done;
        }
    }
done:
    xSemaphoreGive(s_lock);
    return result;
}

// --- backup ---------------------------------------------------------------

cJSON *fans_backup_json(void)
{
    cJSON *b = cJSON_CreateObject();
    cJSON_AddNumberToObject(b, "version", 1);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON_AddItemToObject(b, "rooms", cJSON_Duplicate(s_rooms, true));
    cJSON *fans = cJSON_Duplicate(s_fans, true);
    xSemaphoreGive(s_lock);

    // Inline raw codes so the backup stands alone.
    cJSON *fan, *cmd;
    cJSON_ArrayForEach(fan, fans) {
        cJSON_ArrayForEach(cmd, cJSON_GetObjectItemCaseSensitive(fan, "commands")) {
            const char *raw = jstr(cmd, "raw");
            if (!raw) continue;
            char path[96];
            raw_path(raw, path, sizeof(path));
            uint32_t *d;
            size_t n = storage_read_durations(path, &d);
            cJSON *arr = cJSON_CreateArray();
            for (size_t i = 0; i < n; i++) cJSON_AddItemToArray(arr, cJSON_CreateNumber(d[i]));
            free(d);
            cJSON_AddItemToObject(cmd, "durations", arr);
        }
    }
    cJSON_AddItemToObject(b, "fans", fans);
    return b;
}

esp_err_t fans_restore(const cJSON *backup, char *err)
{
    double version = 0;
    const cJSON *rooms = cJSON_GetObjectItemCaseSensitive(backup, "rooms");
    const cJSON *fans = cJSON_GetObjectItemCaseSensitive(backup, "fans");
    if (!jnum(backup, "version", &version) || version != 1 || !cJSON_IsArray(rooms) || !cJSON_IsArray(fans)) {
        strcpy(err, "This file is not a FanWave backup.");
        return ESP_ERR_INVALID_ARG;
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, rooms) {
        if (!jstr(item, "id") || !valid_name(jstr(item, "name"))) {
            strcpy(err, "The backup has a room without an id or name.");
            return ESP_ERR_INVALID_ARG;
        }
    }
    cJSON_ArrayForEach(item, fans) {
        const char *id = jstr(item, "id");
        if (!id || strlen(id) > 40 || strchr(id, '/') || !valid_name(jstr(item, "name"))) {
            strcpy(err, "The backup has a fan without a valid id or name.");
            return ESP_ERR_INVALID_ARG;
        }
    }

    cJSON *new_fans = cJSON_Duplicate(fans, true);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    // Remove the old raw files, then write the backup's.
    cJSON *fan, *cmd;
    cJSON_ArrayForEach(fan, s_fans) {
        cJSON_ArrayForEach(cmd, cJSON_GetObjectItemCaseSensitive(fan, "commands")) {
            const char *raw = jstr(cmd, "raw");
            if (!raw) continue;
            char path[96];
            raw_path(raw, path, sizeof(path));
            remove(path);
        }
    }
    cJSON_ArrayForEach(fan, new_fans) {
        jset(fan, "confirmed", cJSON_CreateNull());
        annotate(fan);
        cJSON_ArrayForEach(cmd, cJSON_GetObjectItemCaseSensitive(fan, "commands")) {
            cJSON *arr = cJSON_GetObjectItemCaseSensitive(cmd, "durations");
            if (!cJSON_IsArray(arr)) continue;
            size_t n = cJSON_GetArraySize(arr), i = 0;
            uint32_t *d = n ? malloc(n * sizeof(uint32_t)) : NULL;
            cJSON *v;
            if (d) {
                cJSON_ArrayForEach(v, arr) d[i++] = cJSON_IsNumber(v) && v->valuedouble > 0 ? (uint32_t)v->valuedouble : 1;
            }
            char file[64], path[96];
            snprintf(file, sizeof(file), "raw-%.24s-%.12s.bin", jstr(fan, "id"), cmd->string);
            raw_path(file, path, sizeof(path));
            if (d && storage_write_durations(path, d, n) == ESP_OK) jset(cmd, "raw", cJSON_CreateString(file));
            free(d);
            cJSON_DeleteItemFromObjectCaseSensitive(cmd, "durations");
        }
    }
    cJSON_Delete(s_rooms);
    cJSON_Delete(s_fans);
    s_rooms = cJSON_Duplicate(rooms, true);
    s_fans = new_fans;
    save_rooms();
    save_fans();
    xSemaphoreGive(s_lock);
    return ESP_OK;
}
