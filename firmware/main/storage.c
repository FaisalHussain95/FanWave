#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "storage";

#define MAX_JSON (512 * 1024)
#define MAX_DURATIONS 8192

esp_err_t storage_init(void)
{
    esp_vfs_spiffs_conf_t www = {.base_path = WWW_ROOT, .partition_label = "www", .max_files = 6};
    if (esp_vfs_spiffs_register(&www) != ESP_OK) {
        ESP_LOGW(TAG, "no web app in flash; build web/ before the firmware to include it");
    }
    esp_vfs_spiffs_conf_t data = {
        .base_path = DATA_ROOT, .partition_label = "storage", .max_files = 6, .format_if_mount_failed = true,
    };
    ESP_RETURN_ON_ERROR(esp_vfs_spiffs_register(&data), TAG, "mount /data");
    return ESP_OK;
}

size_t storage_free(void)
{
    size_t total = 0, used = 0;
    if (esp_spiffs_info("storage", &total, &used) != ESP_OK) return 0;
    return total > used ? total - used : 0;
}

cJSON *storage_read_json(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    cJSON *json = NULL;
    if (size > 0 && size <= MAX_JSON) {
        char *buf = heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) buf = malloc(size + 1);
        if (buf && fread(buf, 1, size, f) == (size_t)size) {
            buf[size] = '\0';
            json = cJSON_Parse(buf);
            if (!json) ESP_LOGW(TAG, "%s is not valid JSON", path);
        }
        free(buf);
    }
    fclose(f);
    return json;
}

static esp_err_t write_atomic(const char *path, const void *data, size_t len)
{
    char tmp[96];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    ESP_RETURN_ON_FALSE(f, ESP_FAIL, TAG, "open %s", tmp);
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    if (written != len) {
        remove(tmp);
        ESP_LOGE(TAG, "short write to %s (flash full?)", tmp);
        return ESP_ERR_NO_MEM;
    }
    remove(path); // SPIFFS rename won't replace an existing file
    ESP_RETURN_ON_FALSE(rename(tmp, path) == 0, ESP_FAIL, TAG, "rename %s", tmp);
    return ESP_OK;
}

esp_err_t storage_write_json(const char *path, const cJSON *json)
{
    char *text = cJSON_PrintUnformatted(json);
    ESP_RETURN_ON_FALSE(text, ESP_ERR_NO_MEM, TAG, "print");
    esp_err_t err = write_atomic(path, text, strlen(text));
    cJSON_free(text);
    return err;
}

esp_err_t storage_write_durations(const char *path, const uint32_t *d, size_t count)
{
    ESP_RETURN_ON_FALSE(count && count <= MAX_DURATIONS, ESP_ERR_INVALID_SIZE, TAG, "durations");
    uint8_t *buf = malloc(count * 2);
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "alloc");
    for (size_t i = 0; i < count; i++) {
        uint32_t v = d[i] > 0xFFFF ? 0xFFFF : d[i];
        buf[2 * i] = v & 0xFF;
        buf[2 * i + 1] = v >> 8;
    }
    esp_err_t err = write_atomic(path, buf, count * 2);
    free(buf);
    return err;
}

size_t storage_read_durations(const char *path, uint32_t **out)
{
    *out = NULL;
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size < 2 || st.st_size / 2 > MAX_DURATIONS) return 0;
    size_t count = st.st_size / 2;
    uint8_t *buf = malloc(st.st_size);
    uint32_t *d = malloc(count * sizeof(uint32_t));
    FILE *f = fopen(path, "rb");
    size_t n = 0;
    if (buf && d && f && fread(buf, 1, st.st_size, f) == (size_t)st.st_size) {
        for (size_t i = 0; i < count; i++) d[i] = buf[2 * i] | (buf[2 * i + 1] << 8);
        n = count;
        *out = d;
        d = NULL;
    }
    if (f) fclose(f);
    free(buf);
    free(d);
    return n;
}
