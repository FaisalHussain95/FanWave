# FanWave firmware

ESP-IDF firmware for the ESP32-S3-DevKitC-1 (N16R8) with a CC1101 radio.

**Status: first milestone.** It brings up the CC1101, captures and decodes
remote presses, and replays or encodes codes from a serial console. Wi‑Fi,
the HTTP/WebSocket API the web app expects (`web/src/lib/api.ts`), and
storing fans come next. Nothing here has run on real hardware yet.

## Wiring

| CC1101 pin | ESP32-S3 | Notes |
|---|---|---|
| VCC | 3V3 | 3.3 V only; the CC1101 is not 5 V tolerant |
| GND | GND | |
| SCK | GPIO12 | FSPI clock |
| SI (MOSI) | GPIO11 | FSPI data out |
| SO (MISO) | GPIO13 | FSPI data in; also the chip's "ready" signal |
| CSn | GPIO10 | driven by the firmware |
| GDO0 | GPIO4 | radio TX data in (RMT transmit) |
| GDO2 | GPIO5 | radio RX data out (RMT receive) |

The status LED is the board's own RGB LED: GPIO48 on DevKitC-1 v1.0, GPIO38
on v1.1. Every pin can be changed under `idf.py menuconfig` → FanWave → Pins.

The defaults stay clear of the strapping pins (0, 3, 45, 46), USB (19, 20),
the flash and octal PSRAM (26–37) and the console UART (43, 44).

**Pick a module tuned for your remotes.** CC1101 boards have a matching
network for one band. Most sold as "433 MHz" work poorly at the 303–315 MHz
many US ceiling-fan remotes use (Hampton Bay, Harbor Breeze, Minka). Use a
315 MHz module for those; see [`docs/protocols.md`](../docs/protocols.md).

## Build and flash

Needs ESP-IDF 5.4.

```sh
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor   # the DevKitC's "UART" USB port
```

## Console

At the `fanwave>` prompt:

| Command | What it does |
|---|---|
| `status` | Radio found?, frequency, modulation, RSSI |
| `freq 303.9` | Change frequency (MHz) |
| `mod ook` / `mod fsk` | Change modulation |
| `listen on` / `listen off` | Start or stop capturing |
| `protocols` | List the decoders |
| `send pt2262 3A5C16 24 8` | Encode a code and transmit it (protocol, hex, bits, repeats) |
| `replay 3` | Retransmit the last capture exactly as recorded |
| `led off` / `led brightness 20` / `led identify` | Status LED |

Every capture is logged with its timing and any decoder matches:

```
I (5120) fanwave: capture: 100 marks, 98 ms, short 352 µs, long 1047 µs, gap 10871 µs, RSSI -52 dBm
I (5120) fanwave:   pt2262: 0x3A5C16 (24 bits, 4 repeats, base 349 µs, 95%)
```

## Layout

| Path | |
|---|---|
| `components/cc1101` | SPI driver: reset, register setup per band, RX/TX/idle, RSSI |
| `components/rf` | RMT capture on GDO2 (DMA, 4096 edges) and replay on GDO0 |
| `components/protocol` | Fixed-code decoders and encoders; plain C |
| `components/status_led` | WS2812 driver on RMT, and the LED patterns (plain C) |
| `main` | Start-up, capture loop, serial console |
| `test/host` | Host tests for `protocol` and the LED patterns |

Run the host tests (no board or ESP-IDF needed):

```sh
cmake -S test/host -B build-host && cmake --build build-host && ctest --test-dir build-host
```
