#include "status_led.h"

#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "status_led";

// WS2812 at 10 MHz: 0 = 0.3 µs high + 0.9 µs low, 1 = 0.9 + 0.3, reset = 50 µs low.
#define RES_HZ 10000000
#define T_SHORT 3
#define T_LONG 9
#define T_RESET 500
#define FRAME_MS 20

static rmt_channel_handle_t s_ch;
static rmt_encoder_handle_t s_copy;
static SemaphoreHandle_t s_lock;
static status_state_t s_state;
static status_settings_t s_settings;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static esp_err_t show(status_rgb_t c)
{
    // WS2812 takes green, red, blue, most significant bit first.
    uint32_t grb = ((uint32_t)c.g << 16) | ((uint32_t)c.r << 8) | c.b;
    rmt_symbol_word_t sym[25];
    for (int i = 0; i < 24; i++) {
        bool one = grb & (1u << (23 - i));
        sym[i] = (rmt_symbol_word_t){
            .level0 = 1, .duration0 = one ? T_LONG : T_SHORT,
            .level1 = 0, .duration1 = one ? T_SHORT : T_LONG,
        };
    }
    sym[24] = (rmt_symbol_word_t){.level0 = 0, .duration0 = T_RESET / 2, .level1 = 0, .duration1 = T_RESET / 2};
    rmt_transmit_config_t tc = {.loop_count = 0};
    esp_err_t err = rmt_transmit(s_ch, s_copy, sym, sizeof(sym), &tc);
    if (err == ESP_OK) err = rmt_tx_wait_all_done(s_ch, 100);
    return err;
}

static void led_task(void *arg)
{
    status_rgb_t last = {1, 1, 1};
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        status_state_t st = s_state;
        status_settings_t set = s_settings;
        xSemaphoreGive(s_lock);

        status_rgb_t c = status_pattern(&st, &set, now_ms());
        if (c.r != last.r || c.g != last.g || c.b != last.b) {
            if (show(c) == ESP_OK) last = c;
        }
        vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
    }
}

esp_err_t status_led_init(int gpio)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "alloc");
    s_settings = status_default_settings();
    s_state = (status_state_t){.base = STATUS_CONNECTING};

    rmt_tx_channel_config_t tx = {
        .gpio_num = gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RES_HZ,
        .mem_block_symbols = 48,
        .trans_queue_depth = 1,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx, &s_ch), TAG, "RMT channel");
    rmt_copy_encoder_config_t copy = {};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&copy, &s_copy), TAG, "encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(s_ch), TAG, "enable");

    BaseType_t ok = xTaskCreate(led_task, "status_led", 3072, NULL, 3, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}

#define UPDATE(stmt)                               \
    do {                                           \
        if (!s_lock) return;                       \
        xSemaphoreTake(s_lock, portMAX_DELAY);     \
        stmt;                                      \
        xSemaphoreGive(s_lock);                    \
    } while (0)

void status_led_set_base(status_base_t base) { UPDATE(s_state.base = base); }
void status_led_set_error(bool on) { UPDATE(s_state.error = on); }
void status_led_set_updating(bool on) { UPDATE(s_state.updating = on); }
void status_led_set_learning(bool on) { UPDATE(s_state.learning = on); }
void status_led_identify(uint32_t ms) { UPDATE(s_state.identify_until_ms = now_ms() + ms); }
void status_led_set_settings(const status_settings_t *set) { UPDATE(s_settings = *set); }

void status_led_flash(status_flash_t flash)
{
    UPDATE({
        s_state.flash = flash;
        s_state.flash_until_ms = now_ms() + status_flash_ms(flash);
    });
}

status_settings_t status_led_settings(void)
{
    status_settings_t set = status_default_settings();
    if (!s_lock) return set;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    set = s_settings;
    xSemaphoreGive(s_lock);
    return set;
}
