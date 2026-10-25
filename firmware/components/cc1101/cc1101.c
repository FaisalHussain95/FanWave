#include "cc1101.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "cc1101";

#define XOSC_HZ 26000000ULL

// Configuration registers.
enum {
    REG_IOCFG2 = 0x00, REG_IOCFG1, REG_IOCFG0, REG_FIFOTHR, REG_SYNC1, REG_SYNC0, REG_PKTLEN,
    REG_PKTCTRL1, REG_PKTCTRL0, REG_ADDR, REG_CHANNR, REG_FSCTRL1, REG_FSCTRL0, REG_FREQ2,
    REG_FREQ1, REG_FREQ0, REG_MDMCFG4, REG_MDMCFG3, REG_MDMCFG2, REG_MDMCFG1, REG_MDMCFG0,
    REG_DEVIATN, REG_MCSM2, REG_MCSM1, REG_MCSM0, REG_FOCCFG, REG_BSCFG, REG_AGCCTRL2,
    REG_AGCCTRL1, REG_AGCCTRL0, REG_WOREVT1, REG_WOREVT0, REG_WORCTRL, REG_FREND1, REG_FREND0,
    REG_FSCAL3, REG_FSCAL2, REG_FSCAL1, REG_FSCAL0, REG_RCCTRL1, REG_RCCTRL0, REG_FSTEST,
    REG_PTEST, REG_AGCTEST, REG_TEST2, REG_TEST1, REG_TEST0,
};

// Status registers, read with the burst bit set.
enum { ST_PARTNUM = 0x30, ST_VERSION = 0x31, ST_RSSI = 0x34, ST_MARCSTATE = 0x35 };

// Command strobes.
enum { S_SRES = 0x30, S_SCAL = 0x33, S_SRX = 0x34, S_STX = 0x35, S_SIDLE = 0x36, S_SFRX = 0x3A, S_SFTX = 0x3B };

#define PATABLE 0x3E
#define READ 0x80
#define BURST 0x40

#define MARCSTATE_IDLE 0x01
#define MARCSTATE_RX 0x0D

static spi_device_handle_t s_dev;
static cc1101_pins_t s_pins;
static SemaphoreHandle_t s_lock;

/** CSn low, then wait for the chip to pull SO low, which means its crystal is running. */
static esp_err_t select(void)
{
    gpio_set_level(s_pins.cs, 0);
    int64_t deadline = esp_timer_get_time() + 10000;
    while (gpio_get_level(s_pins.miso)) {
        if (esp_timer_get_time() > deadline) {
            gpio_set_level(s_pins.cs, 1);
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

static void deselect(void)
{
    gpio_set_level(s_pins.cs, 1);
}

static esp_err_t transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_transaction_t t = {.length = len * 8, .tx_buffer = tx, .rx_buffer = rx};
    ESP_RETURN_ON_ERROR(select(), TAG, "chip not ready (SO stayed high)");
    esp_err_t err = spi_device_polling_transmit(s_dev, &t);
    deselect();
    return err;
}

static esp_err_t strobe(uint8_t cmd)
{
    uint8_t rx;
    return transfer(&cmd, &rx, 1);
}

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = {reg, value}, rx[2];
    return transfer(tx, rx, 2);
}

static esp_err_t write_burst(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t tx[1 + 47], rx[1 + 47];
    if (len > 47) return ESP_ERR_INVALID_SIZE;
    tx[0] = reg | BURST;
    memcpy(tx + 1, data, len);
    return transfer(tx, rx, len + 1);
}

static esp_err_t read_status(uint8_t reg, uint8_t *value)
{
    uint8_t tx[2] = {reg | READ | BURST, 0}, rx[2];
    esp_err_t err = transfer(tx, rx, 2);
    *value = rx[1];
    return err;
}

static esp_err_t wait_idle(uint32_t timeout_ms)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    uint8_t state = 0;
    do {
        ESP_RETURN_ON_ERROR(read_status(ST_MARCSTATE, &state), TAG, "read MARCSTATE");
        if ((state & 0x1F) == MARCSTATE_IDLE) return ESP_OK;
        esp_rom_delay_us(50);
    } while (esp_timer_get_time() < deadline);
    return ESP_ERR_TIMEOUT;
}

/** FREQ2..0: the carrier in units of XOSC / 2^16. */
static uint32_t freq_word(uint32_t hz)
{
    return (uint32_t)(((uint64_t)hz << 16) / XOSC_HZ);
}

/**
 * Below 430.6 MHz the VCO needs different calibration settings (FSCAL2,
 * TEST0); with the 433 MHz values the synthesiser doesn't lock cleanly around
 * 303–315 MHz. Values as reported by fan projects using 303 MHz remotes;
 * confirm on hardware.
 */
static bool low_vco_band(uint32_t hz)
{
    return hz < 430600000;
}

cc1101_config_t cc1101_default_config(void)
{
    return (cc1101_config_t){
        .frequency_hz = 433920000,
        .modulation = CC1101_MOD_OOK,
        .rx_bandwidth_hz = 325000,
        .deviation_hz = 47600,
    };
}

bool cc1101_frequency_supported(uint32_t hz)
{
    return (hz >= 300000000 && hz <= 348000000) || (hz >= 387000000 && hz <= 464000000) ||
           (hz >= 779000000 && hz <= 928000000);
}

/** MDMCFG4 CHANBW bits for the widest bandwidth not above the request. */
static uint8_t bandwidth_bits(uint32_t hz)
{
    uint8_t best = 0xF0; // E=3, M=3: 58 kHz, the narrowest
    uint64_t best_bw = 0;
    for (int e = 0; e < 4; e++) {
        for (int m = 0; m < 4; m++) {
            uint64_t bw = XOSC_HZ / (8ULL * (4 + m) << e);
            if (bw <= hz && bw > best_bw) {
                best_bw = bw;
                best = (uint8_t)((e << 6) | (m << 4));
            }
        }
    }
    return best;
}

/** DEVIATN for the closest supported deviation. */
static uint8_t deviation_bits(uint32_t hz)
{
    uint8_t best = 0x47;
    uint64_t best_err = UINT64_MAX;
    for (int e = 0; e < 8; e++) {
        for (int m = 0; m < 8; m++) {
            uint64_t dev = (XOSC_HZ * (8 + m) << e) >> 17;
            uint64_t err = dev > hz ? dev - hz : hz - dev;
            if (err < best_err) {
                best_err = err;
                best = (uint8_t)((e << 4) | m);
            }
        }
    }
    return best;
}

esp_err_t cc1101_init(const cc1101_pins_t *pins)
{
    s_pins = *pins;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();

    // CSn is driven by hand so we can wait for SO to go low before each access.
    gpio_config_t cs = {.pin_bit_mask = 1ULL << pins->cs, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&cs), TAG, "CS pin");
    gpio_set_level(pins->cs, 1);

    spi_bus_config_t bus = {
        .sclk_io_num = pins->sck,
        .mosi_io_num = pins->mosi,
        .miso_io_num = pins->miso,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(pins->host, &bus, SPI_DMA_DISABLED), TAG, "SPI bus");
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 4 * 1000 * 1000, // under the 6.5 MHz burst limit
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(pins->host, &dev, &s_dev), TAG, "SPI device");

    // Manual power-on reset (datasheet 19.1.2): strobe CSn, wait, then SRES.
    gpio_set_level(pins->cs, 0);
    esp_rom_delay_us(10);
    gpio_set_level(pins->cs, 1);
    esp_rom_delay_us(45);
    ESP_RETURN_ON_ERROR(strobe(S_SRES), TAG, "reset");
    ESP_RETURN_ON_ERROR(select(), TAG, "chip did not come out of reset");
    deselect();

    uint8_t partnum, version;
    ESP_RETURN_ON_ERROR(cc1101_chip_id(&partnum, &version), TAG, "chip id");
    // VERSION reads 0x14 on current silicon and 0x04 on early parts; 0x00 or
    // 0xFF means nothing is answering on the bus.
    if (version == 0x00 || version == 0xFF) {
        ESP_LOGE(TAG, "no CC1101 found (VERSION=0x%02x); check wiring and 3.3 V supply", version);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "CC1101 found: PARTNUM=0x%02x VERSION=0x%02x", partnum, version);
    return ESP_OK;
}

esp_err_t cc1101_chip_id(uint8_t *partnum, uint8_t *version)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = read_status(ST_PARTNUM, partnum);
    if (err == ESP_OK) err = read_status(ST_VERSION, version);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t cc1101_configure(const cc1101_config_t *c)
{
    ESP_RETURN_ON_FALSE(cc1101_frequency_supported(c->frequency_hz), ESP_ERR_INVALID_ARG, TAG,
                        "%lu Hz is outside the CC1101 bands", (unsigned long)c->frequency_hz);
    bool ook = c->modulation == CC1101_MOD_OOK;
    uint32_t freq = freq_word(c->frequency_hz);
    bool low_vco = low_vco_band(c->frequency_hz);

    const uint8_t regs[][2] = {
        {REG_IOCFG2, 0x0D},   // GDO2: asynchronous serial data out (RX)
        {REG_IOCFG1, 0x2E},   // GDO1 (shared with SO): high impedance when CSn is high
        {REG_IOCFG0, 0x2E},   // GDO0: high impedance; in async TX it is the data input
        {REG_FIFOTHR, 0x47},
        {REG_PKTCTRL1, 0x04},
        {REG_PKTCTRL0, 0x32}, // asynchronous serial mode, infinite length, no CRC
        {REG_CHANNR, 0x00},
        {REG_FSCTRL1, 0x06},  // IF 152 kHz
        {REG_FSCTRL0, 0x00},
        {REG_FREQ2, (uint8_t)(freq >> 16)},
        {REG_FREQ1, (uint8_t)(freq >> 8)},
        {REG_FREQ0, (uint8_t)freq},
        {REG_MDMCFG4, (uint8_t)(bandwidth_bits(c->rx_bandwidth_hz) | 0x0A)}, // DRATE_E=10
        {REG_MDMCFG3, 0x83},  // ~38 kBaud: only sets demodulator timing in async mode
        {REG_MDMCFG2, ook ? 0x30 : 0x00}, // ASK/OOK or 2-FSK, no sync word
        {REG_MDMCFG1, 0x02},
        {REG_MDMCFG0, 0xF8},
        {REG_DEVIATN, deviation_bits(c->deviation_hz)},
        {REG_MCSM2, 0x07},
        {REG_MCSM1, 0x30},
        {REG_MCSM0, 0x18},    // calibrate when leaving idle
        {REG_FOCCFG, 0x16},
        {REG_BSCFG, 0x6C},
        // AGC tuned for OOK per TI DN022; the FSK values are SmartRF defaults.
        {REG_AGCCTRL2, ook ? 0x03 : 0x43},
        {REG_AGCCTRL1, ook ? 0x00 : 0x40},
        {REG_AGCCTRL0, 0x91},
        {REG_FREND1, 0x56},
        {REG_FREND0, ook ? 0x11 : 0x10}, // OOK: PATABLE[0] for 0, PATABLE[1] for 1
        {REG_FSCAL3, 0xE9},
        {REG_FSCAL2, low_vco ? 0x0A : 0x2A}, // see low_vco_band()
        {REG_FSCAL1, 0x00},
        {REG_FSCAL0, 0x1F},
        {REG_TEST2, 0x81},
        {REG_TEST1, 0x35},
        {REG_TEST0, low_vco ? 0x0B : 0x09},
    };

    // About +10 dBm. The best value differs a little per band (TI DN013).
    uint8_t pa = c->frequency_hz < 348000000 ? 0xC2 : c->frequency_hz < 464000000 ? 0xC0 : 0xC5;
    uint8_t patable[2] = {ook ? 0x00 : pa, pa};

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = strobe(S_SIDLE);
    for (size_t i = 0; err == ESP_OK && i < sizeof(regs) / sizeof(regs[0]); i++) {
        err = write_reg(regs[i][0], regs[i][1]);
    }
    if (err == ESP_OK) err = write_burst(PATABLE, patable, ook ? 2 : 1);
    if (err == ESP_OK) err = strobe(S_SCAL);
    if (err == ESP_OK) err = wait_idle(10);
    xSemaphoreGive(s_lock);
    ESP_RETURN_ON_ERROR(err, TAG, "configure");
    ESP_LOGI(TAG, "configured %.3f MHz %s", c->frequency_hz / 1e6, ook ? "OOK" : "2-FSK");
    return ESP_OK;
}

static esp_err_t locked_strobe(uint8_t cmd)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = strobe(cmd);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t cc1101_idle(void)
{
    return locked_strobe(S_SIDLE);
}

esp_err_t cc1101_rx(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = strobe(S_SIDLE);
    if (err == ESP_OK) err = strobe(S_SFRX);
    if (err == ESP_OK) err = strobe(S_SRX);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t cc1101_tx(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = strobe(S_SIDLE);
    if (err == ESP_OK) err = strobe(S_SFTX);
    if (err == ESP_OK) err = strobe(S_STX);
    xSemaphoreGive(s_lock);
    return err;
}

int cc1101_rssi_dbm(void)
{
    uint8_t raw = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    read_status(ST_RSSI, &raw);
    xSemaphoreGive(s_lock);
    // Datasheet 17.3: two's complement in half-dB steps, offset 74 dB.
    int v = raw >= 128 ? (int)raw - 256 : raw;
    return v / 2 - 74;
}

esp_err_t cc1101_tune_rx(uint32_t hz)
{
    ESP_RETURN_ON_FALSE(cc1101_frequency_supported(hz), ESP_ERR_INVALID_ARG, TAG, "%lu Hz is outside the CC1101 bands",
                        (unsigned long)hz);
    uint32_t freq = freq_word(hz);
    uint8_t words[3] = {(uint8_t)(freq >> 16), (uint8_t)(freq >> 8), (uint8_t)freq};
    bool low_vco = low_vco_band(hz);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = strobe(S_SIDLE);
    if (err == ESP_OK) err = wait_idle(2);
    if (err == ESP_OK) err = write_burst(REG_FREQ2, words, sizeof(words));
    if (err == ESP_OK) err = write_reg(REG_FSCAL2, low_vco ? 0x0A : 0x2A);
    if (err == ESP_OK) err = write_reg(REG_TEST0, low_vco ? 0x0B : 0x09);
    // MCSM0 calibrates on the way out of idle, so SRX recalibrates for the new frequency.
    if (err == ESP_OK) err = strobe(S_SFRX);
    if (err == ESP_OK) err = strobe(S_SRX);
    xSemaphoreGive(s_lock);
    return err;
}

bool cc1101_in_rx(void)
{
    uint8_t state = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = read_status(ST_MARCSTATE, &state);
    xSemaphoreGive(s_lock);
    return err == ESP_OK && (state & 0x1F) == MARCSTATE_RX;
}
