#include "captures.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define KEEP 20

static capture_t s_ring[KEEP];
static size_t s_next; // slot the next capture goes into
static uint32_t s_next_id = 1;
static SemaphoreHandle_t s_lock;

int64_t uptime_ms(void)
{
    return esp_timer_get_time() / 1000;
}

void captures_init(void)
{
    s_lock = xSemaphoreCreateMutex();
}

uint32_t captures_add(radio_capture_t *rf, const cc1101_config_t *radio, capture_t *out)
{
    capture_t c = {
        .at_ms = rf->at_us / 1000,
        .radio = *radio,
        .rssi_dbm = rf->rssi_dbm,
        .durations = rf->durations,
        .count = rf->count,
    };
    rf->durations = NULL;
    c.timing = fw_analyze(c.durations, c.count);
    c.match_count = fw_decode(c.durations, c.count, c.matches, CAPTURE_MATCHES);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    c.id = s_next_id++;
    free(s_ring[s_next].durations);
    s_ring[s_next] = c;
    s_next = (s_next + 1) % KEEP;
    xSemaphoreGive(s_lock);

    if (out) {
        *out = c;
        out->durations = NULL;
        out->count = c.count;
    }
    return c.id;
}

bool captures_get(uint32_t id, capture_t *out)
{
    bool found = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < KEEP && !found; i++) {
        if (s_ring[i].durations && s_ring[i].id == id) {
            *out = s_ring[i];
            out->durations = malloc(out->count * sizeof(uint32_t));
            if (out->durations) {
                memcpy(out->durations, s_ring[i].durations, out->count * sizeof(uint32_t));
                found = true;
            }
        }
    }
    xSemaphoreGive(s_lock);
    return found;
}

void captures_free(capture_t *c)
{
    free(c->durations);
    c->durations = NULL;
}

void bits_to_string(uint64_t bits, uint8_t count, char *out)
{
    for (int i = 0; i < count; i++) out[i] = (bits >> (count - 1 - i)) & 1 ? '1' : '0';
    out[count] = '\0';
}

static void hex(uint64_t v, uint8_t bit_count, char *out, size_t size)
{
    snprintf(out, size, "0x%0*llX", (bit_count + 3) / 4, (unsigned long long)v);
}

/** "0011 1010 0101" */
static void nibbles(const char *bits, char *out)
{
    size_t n = strlen(bits), w = 0;
    for (size_t i = 0; i < n; i++) {
        if (i && (n - i) % 4 == 0) out[w++] = ' ';
        out[w++] = bits[i];
    }
    out[w] = '\0';
}

static void add_field(cJSON *frame, const char *label, const char *shrt, int weight, const char *bits,
                      const char *value, const char *kind)
{
    cJSON *f = cJSON_CreateObject();
    cJSON_AddStringToObject(f, "label", label);
    cJSON_AddStringToObject(f, "short", shrt);
    cJSON_AddNumberToObject(f, "weight", weight);
    cJSON_AddStringToObject(f, "bits", bits);
    cJSON_AddStringToObject(f, "value", value);
    cJSON_AddStringToObject(f, "kind", kind);
    cJSON_AddItemToArray(frame, f);
}

cJSON *match_json(const fw_match_t *m)
{
    const fw_protocol_t *p = m->protocol;
    char note[96], bits[65], spaced[96], value[24];
    snprintf(note, sizeof(note), "%u bits · base %u µs · %u repeat%s", m->bit_count, m->base_us, m->repeats,
             m->repeats == 1 ? "" : "s");

    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "protocol", p->id);
    cJSON_AddStringToObject(o, "name", p->name);
    cJSON_AddNumberToObject(o, "confidence", (int)(m->confidence * 100 + 0.5f) / 100.0);
    cJSON_AddStringToObject(o, "note", note);
    cJSON_AddNumberToObject(o, "bitCount", m->bit_count);
    cJSON_AddNumberToObject(o, "baseUs", m->base_us);
    bits_to_string(m->bits, m->bit_count, bits);
    cJSON_AddStringToObject(o, "bits", bits);

    cJSON *frame = cJSON_AddArrayToObject(o, "frame");
    snprintf(value, sizeof(value), "%u:%u", p->sync.mark, p->sync.space);
    add_field(frame, "Sync", "S", 2, "—", value, "sync");

    uint8_t addr = p->address_bits && p->address_bits < m->bit_count ? p->address_bits : 0;
    if (addr) {
        char part[65];
        memcpy(part, bits, addr);
        part[addr] = '\0';
        nibbles(part, spaced);
        hex(m->bits >> (m->bit_count - addr), addr, value, sizeof(value));
        add_field(frame, "Address", "Address", 10, spaced, value, "address");
        cJSON_AddStringToObject(o, "address", value);
    }
    uint8_t cmd = m->bit_count - addr;
    nibbles(bits + addr, spaced);
    hex(m->bits & (cmd >= 64 ? ~0ULL : ((1ULL << cmd) - 1)), cmd, value, sizeof(value));
    add_field(frame, addr ? "Command" : "Data", addr ? "Cmd" : "Data", addr ? 3 : 10, spaced, value, "command");
    cJSON_AddStringToObject(o, "command", value);
    return o;
}

cJSON *capture_json(const capture_t *c)
{
    const fw_match_t *best = c->match_count ? &c->matches[0] : NULL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", c->id);
    cJSON_AddNumberToObject(o, "at", (double)c->at_ms);
    cJSON_AddStringToObject(o, "modulation", c->radio.modulation == CC1101_MOD_OOK ? "OOK" : "FSK");
    cJSON_AddNumberToObject(o, "frequencyMHz", c->radio.frequency_hz / 1e6);
    cJSON_AddNumberToObject(o, "rssi", c->rssi_dbm);
    cJSON_AddNumberToObject(o, "repeats", best ? best->repeats : 1);
    if (best) {
        char bits[65];
        bits_to_string(best->bits, best->bit_count, bits);
        cJSON_AddStringToObject(o, "bits", bits);
    } else {
        cJSON_AddNullToObject(o, "bits");
    }
    cJSON_AddNumberToObject(o, "pulseCount", c->timing.pulse_count);
    if (c->timing.short_us) cJSON_AddNumberToObject(o, "shortUs", c->timing.short_us);
    else cJSON_AddNullToObject(o, "shortUs");
    if (c->timing.long_us) cJSON_AddNumberToObject(o, "longUs", c->timing.long_us);
    else cJSON_AddNullToObject(o, "longUs");
    if (c->timing.gap_us) cJSON_AddNumberToObject(o, "syncMs", (int)(c->timing.gap_us / 100) / 10.0);
    else cJSON_AddNullToObject(o, "syncMs");
    cJSON_AddNumberToObject(o, "durationMs", (int)(c->timing.total_us / 100) / 10.0);
    cJSON_AddStringToObject(o, "label", best ? best->protocol->name : "Raw");
    return o;
}

cJSON *captures_list_json(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "now", (double)uptime_ms());
    cJSON *list = cJSON_AddArrayToObject(o, "captures");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t k = 1; k <= KEEP; k++) {
        const capture_t *c = &s_ring[(s_next + KEEP - k) % KEEP];
        if (c->durations) cJSON_AddItemToArray(list, capture_json(c));
    }
    xSemaphoreGive(s_lock);
    return o;
}

cJSON *captures_matches_json(uint32_t id)
{
    cJSON *arr = NULL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < KEEP; i++) {
        const capture_t *c = &s_ring[i];
        if (c->durations && c->id == id) {
            arr = cJSON_CreateArray();
            for (size_t j = 0; j < c->match_count; j++) cJSON_AddItemToArray(arr, match_json(&c->matches[j]));
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return arr;
}
