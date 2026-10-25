#include "status_pattern.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

static int is_off(status_rgb_t c) { return c.r == 0 && c.g == 0 && c.b == 0; }
static int is_red(status_rgb_t c) { return c.r > 0 && c.g == 0 && c.b == 0; }

int main(void)
{
    status_settings_t set = status_default_settings();
    status_state_t st = {.base = STATUS_ONLINE};

    // Online and idle: dim accent, or off when the user prefers.
    status_rgb_t idle = status_pattern(&st, &set, 1000);
    CHECK(!is_off(idle) && idle.b > idle.g);
    set.idle_dim = false;
    CHECK(is_off(status_pattern(&st, &set, 1000)));
    set.idle_dim = true;

    // A TX flash shows for its duration, then the base state returns.
    st.flash = STATUS_FLASH_TX;
    st.flash_until_ms = 1000 + status_flash_ms(STATUS_FLASH_TX);
    status_rgb_t flash = status_pattern(&st, &set, 1010);
    CHECK(flash.b > idle.b);
    status_rgb_t after = status_pattern(&st, &set, 1000 + status_flash_ms(STATUS_FLASH_TX));
    CHECK(after.r == idle.r && after.g == idle.g && after.b == idle.b);

    // Radio flashes can be turned off; success flashes still show.
    set.radio_flashes = false;
    status_rgb_t muted = status_pattern(&st, &set, 1010);
    CHECK(muted.r == idle.r && muted.g == idle.g && muted.b == idle.b);
    st.flash = STATUS_FLASH_SUCCESS;
    st.flash_until_ms = 2000;
    status_rgb_t ok = status_pattern(&st, &set, 1500);
    CHECK(ok.g > ok.r && ok.g > ok.b);
    set.radio_flashes = true;
    st.flash = STATUS_FLASH_NONE;
    st.flash_until_ms = 0;

    // Errors beat everything below them and show even with the LED disabled.
    st.updating = true;
    st.learning = true;
    st.error = true;
    CHECK(is_red(status_pattern(&st, &set, 3000)));
    set.enabled = false;
    CHECK(is_red(status_pattern(&st, &set, 3000)));
    set.brightness = 0;
    CHECK(is_red(status_pattern(&st, &set, 3000))); // floored at 20%
    set.brightness = 40;
    st.error = false;
    CHECK(is_off(status_pattern(&st, &set, 3000)));
    set.enabled = true;

    // Updating beats learning; it blinks.
    CHECK(!is_off(status_pattern(&st, &set, 3000)) || !is_off(status_pattern(&st, &set, 3250)));
    CHECK(is_off(status_pattern(&st, &set, 3250)) != is_off(status_pattern(&st, &set, 3000)));
    st.updating = false;

    // Learning breathes: dimmer at the start of a cycle than in the middle.
    status_rgb_t low = status_pattern(&st, &set, 4000);
    status_rgb_t high = status_pattern(&st, &set, 5000);
    CHECK(high.b > low.b);
    st.learning = false;

    // Identify blinks white over everything, even an error.
    st.error = true;
    st.identify_until_ms = 7000;
    status_rgb_t id = status_pattern(&st, &set, 6000);
    CHECK(id.r == id.g && id.g == id.b && id.r > 0);
    CHECK(is_red(status_pattern(&st, &set, 7000)));

    // Setup mode pulses blue; connecting blinks.
    st = (status_state_t){.base = STATUS_SETUP_AP};
    status_rgb_t ap = status_pattern(&st, &set, 1500);
    CHECK(ap.b > ap.r && ap.b > ap.g);
    st.base = STATUS_CONNECTING;
    CHECK(is_off(status_pattern(&st, &set, 100)) != is_off(status_pattern(&st, &set, 0)));

    // Brightness scales output.
    st.base = STATUS_SETUP_AP;
    set.brightness = 100;
    status_rgb_t bright = status_pattern(&st, &set, 1500);
    set.brightness = 20;
    status_rgb_t dim = status_pattern(&st, &set, 1500);
    CHECK(bright.b > dim.b);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("status_pattern: all checks passed\n");
    return 0;
}
