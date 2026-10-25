// TI CC1101 sub-GHz transceiver, used in asynchronous serial mode: the radio
// demodulates OOK/FSK onto GDO2 for the RMT receiver, and modulates whatever
// the RMT transmitter drives onto GDO0. No packet engine, no FIFO.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    spi_host_device_t host;
    int sck;
    int mosi;
    int miso;
    int cs;
} cc1101_pins_t;

typedef enum {
    CC1101_MOD_OOK = 0,
    CC1101_MOD_2FSK = 1,
} cc1101_modulation_t;

typedef struct {
    uint32_t frequency_hz;
    cc1101_modulation_t modulation;
    uint32_t rx_bandwidth_hz; // rounded down to the nearest supported step
    uint32_t deviation_hz;    // 2-FSK only
} cc1101_config_t;

/** Defaults: 433.92 MHz OOK, 325 kHz receive bandwidth, 47.6 kHz deviation. */
cc1101_config_t cc1101_default_config(void);

/** Set up SPI, reset the chip and check it answers. Leaves it idle. */
esp_err_t cc1101_init(const cc1101_pins_t *pins);

/** Apply a radio configuration. The chip is left idle; call cc1101_rx() or cc1101_tx() after. */
esp_err_t cc1101_configure(const cc1101_config_t *config);

/** True if the frequency falls in one of the CC1101's bands (300–348, 387–464, 779–928 MHz). */
bool cc1101_frequency_supported(uint32_t hz);

esp_err_t cc1101_idle(void);
esp_err_t cc1101_rx(void);
esp_err_t cc1101_tx(void);

/**
 * Retune and enter RX, rewriting only the frequency and its VCO calibration
 * settings; everything else stays as cc1101_configure() left it. Quick enough
 * to step across a band. RX starts after a ~800 µs calibration: poll
 * cc1101_in_rx() before trusting cc1101_rssi_dbm().
 */
esp_err_t cc1101_tune_rx(uint32_t hz);

/** True once the chip has finished calibrating and is receiving. */
bool cc1101_in_rx(void);

/** Received signal strength right now, in dBm. Only meaningful in RX. */
int cc1101_rssi_dbm(void);

/** Chip PARTNUM and VERSION registers, for diagnostics. */
esp_err_t cc1101_chip_id(uint8_t *partnum, uint8_t *version);

#ifdef __cplusplus
}
#endif
