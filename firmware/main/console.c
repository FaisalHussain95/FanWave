// Serial console for bringing up the radio before the web API exists.
// Type "help" at the fanwave> prompt.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cc1101.h"
#include "esp_console.h"
#include "esp_log.h"
#include "fw_protocol.h"
#include "hub.h"
#include "rf.h"
#include "status_led.h"

#define MAX_TX 4096

static int cmd_status(int argc, char **argv)
{
    uint8_t partnum = 0, version = 0;
    esp_err_t err = cc1101_chip_id(&partnum, &version);
    cc1101_config_t c = hub_radio();
    printf("radio:     %s (PARTNUM 0x%02x, VERSION 0x%02x)\n", err == ESP_OK ? "ok" : esp_err_to_name(err), partnum,
           version);
    printf("frequency: %.3f MHz %s\n", c.frequency_hz / 1e6, c.modulation == CC1101_MOD_OOK ? "OOK" : "2-FSK");
    printf("listening: %s\n", rf_listening() ? "yes" : "no");
    if (rf_listening()) printf("rssi:      %d dBm\n", cc1101_rssi_dbm());
    return 0;
}

static int cmd_freq(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: freq <MHz>, e.g. freq 433.92 or freq 303.9\n");
        return 1;
    }
    double mhz = atof(argv[1]);
    cc1101_config_t c = hub_radio();
    c.frequency_hz = (uint32_t)(mhz * 1e6 + 0.5);
    if (!cc1101_frequency_supported(c.frequency_hz)) {
        printf("%.3f MHz is outside the CC1101 bands (300-348, 387-464, 779-928 MHz)\n", mhz);
        return 1;
    }
    esp_err_t err = hub_set_radio(&c);
    printf("%s\n", err == ESP_OK ? "ok" : esp_err_to_name(err));
    return err != ESP_OK;
}

static int cmd_mod(int argc, char **argv)
{
    cc1101_config_t c = hub_radio();
    if (argc == 2 && strcasecmp(argv[1], "ook") == 0) c.modulation = CC1101_MOD_OOK;
    else if (argc == 2 && strcasecmp(argv[1], "fsk") == 0) c.modulation = CC1101_MOD_2FSK;
    else {
        printf("usage: mod ook|fsk\n");
        return 1;
    }
    esp_err_t err = hub_set_radio(&c);
    printf("%s\n", err == ESP_OK ? "ok" : esp_err_to_name(err));
    return err != ESP_OK;
}

static int cmd_listen(int argc, char **argv)
{
    if (argc != 2 || (strcmp(argv[1], "on") && strcmp(argv[1], "off"))) {
        printf("usage: listen on|off\n");
        return 1;
    }
    esp_err_t err = rf_set_listening(strcmp(argv[1], "on") == 0);
    printf("%s\n", err == ESP_OK ? "ok" : esp_err_to_name(err));
    return err != ESP_OK;
}

static int cmd_protocols(int argc, char **argv)
{
    size_t n;
    const fw_protocol_t *p = fw_protocols(&n);
    for (size_t i = 0; i < n; i++) {
        printf("%-14s %4u µs  sync %u:%u  %u-%u bits%s  %s\n", p[i].id, p[i].base_us, p[i].sync.mark,
               p[i].sync.space, p[i].min_bits, p[i].max_bits, p[i].inverted ? "  inverted" : "", p[i].name);
    }
    return 0;
}

static int cmd_send(int argc, char **argv)
{
    if (argc < 4) {
        printf("usage: send <protocol> <hex code> <bits> [repeats=8] [base µs]\n"
               "  e.g. send pt2262 3A5C16 24\n");
        return 1;
    }
    const fw_protocol_t *p = fw_protocol_find(argv[1]);
    if (!p) {
        printf("unknown protocol \"%s\"; see \"protocols\"\n", argv[1]);
        return 1;
    }
    uint64_t bits = strtoull(argv[2], NULL, 16);
    int count = atoi(argv[3]);
    int repeats = argc > 4 ? atoi(argv[4]) : 8;
    int base = argc > 5 ? atoi(argv[5]) : 0;
    if (count < 1 || count > 64 || repeats < 1 || repeats > 50 || base < 0 || base > 5000) {
        printf("bits must be 1-64, repeats 1-50, base 0-5000 µs\n");
        return 1;
    }
    uint32_t *d = malloc(MAX_TX * sizeof(uint32_t));
    if (!d) return 1;
    size_t n = fw_encode(p, bits, (uint8_t)count, (uint16_t)repeats, (uint16_t)base, d, MAX_TX);
    esp_err_t err = n ? hub_transmit(d, n) : ESP_ERR_INVALID_SIZE;
    free(d);
    printf("%s\n", err == ESP_OK ? "sent" : esp_err_to_name(err));
    return err != ESP_OK;
}

static int cmd_replay(int argc, char **argv)
{
    int times = argc > 1 ? atoi(argv[1]) : 1;
    if (times < 1 || times > 20) {
        printf("usage: replay [times 1-20]\n");
        return 1;
    }
    uint32_t *d;
    size_t n = hub_last_capture(&d);
    if (!n) {
        printf("nothing captured yet\n");
        return 1;
    }
    esp_err_t err = ESP_OK;
    for (int i = 0; i < times && err == ESP_OK; i++) err = hub_transmit(d, n);
    free(d);
    printf("%s\n", err == ESP_OK ? "sent" : esp_err_to_name(err));
    return err != ESP_OK;
}

static int cmd_led(int argc, char **argv)
{
    status_settings_t s = status_led_settings();
    if (argc == 2 && strcmp(argv[1], "on") == 0) s.enabled = true;
    else if (argc == 2 && strcmp(argv[1], "off") == 0) s.enabled = false;
    else if (argc == 2 && strcmp(argv[1], "identify") == 0) {
        status_led_identify(5000);
        return 0;
    } else if (argc == 3 && strcmp(argv[1], "brightness") == 0) {
        int b = atoi(argv[2]);
        if (b < 0 || b > 100) {
            printf("brightness is 0-100\n");
            return 1;
        }
        s.brightness = (uint8_t)b;
    } else {
        printf("usage: led on|off|identify|brightness <0-100>\n");
        return 1;
    }
    status_led_set_settings(&s);
    return 0;
}

void console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "fanwave>";
    rc.max_cmdline_length = 128;
    esp_console_dev_uart_config_t uc = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uc, &rc, &repl));

    const esp_console_cmd_t cmds[] = {
        {.command = "status", .help = "Radio state, frequency and RSSI", .func = cmd_status},
        {.command = "freq", .help = "Set the radio frequency in MHz", .hint = "<MHz>", .func = cmd_freq},
        {.command = "mod", .help = "Set modulation", .hint = "ook|fsk", .func = cmd_mod},
        {.command = "listen", .help = "Start or stop capturing", .hint = "on|off", .func = cmd_listen},
        {.command = "protocols", .help = "List known protocols", .func = cmd_protocols},
        {.command = "send", .help = "Encode and transmit a code",
         .hint = "<protocol> <hex> <bits> [repeats] [base_us]", .func = cmd_send},
        {.command = "replay", .help = "Retransmit the last capture as recorded", .hint = "[times]",
         .func = cmd_replay},
        {.command = "led", .help = "Status LED settings", .hint = "on|off|identify|brightness <0-100>",
         .func = cmd_led},
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    ESP_ERROR_CHECK(esp_console_register_help_command());
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
