# FanWave firmware

ESP-IDF firmware for the ESP32-S3-DevKitC-1 (N16R8) with a CC1101 radio.

It runs the radio, joins your Wi‑Fi, serves the web app and the API it uses
([docs/api.md](../docs/api.md)), stores fans and their learned codes, and
takes firmware updates over the air. A serial console is there for bring-up.
**Nothing here has run on real hardware yet.**

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

## Flash a release from the browser

Every `v*` tag builds a GitHub release with two files:

| File | Use |
|---|---|
| `fanwave-<version>-esp32s3-full.bin` | First install, or any USB flash: bootloader, partition table, firmware and web app in one image, written at **0x0** |
| `fanwave-<version>-esp32s3-ota.bin` | Updates over Wi‑Fi: Settings → Firmware → Choose file (firmware only, not the web app) |

To flash the full image without installing anything:

1. Plug the board's **UART** USB port into a computer and open
   https://espressif.github.io/esptool-js/ in Chrome or Edge (Web Serial).
2. Click **Connect** and pick the board's serial port. If it won't connect,
   hold **BOOT**, tap **RST**, then release BOOT.
3. Add `fanwave-…-full.bin` at flash address `0x0` and click **Program**.
4. Tap **RST**. The hub starts its setup network (see First start below).

The image doesn't cover the NVS or `storage` partitions, so a reflash keeps
the saved Wi‑Fi network, fans and learned codes. Use **Erase Flash** first
for a clean start.

To cut a release: `git tag v0.3.0 && git push origin v0.3.0`. CI
(`.github/workflows/build.yml`) also builds both files for every push to
`main` and every pull request; they're attached to the workflow run as an
artifact.

## Build and flash

Needs ESP-IDF 5.4 and Node 20+. Build the web app first: the firmware build
packs `web/dist` into the `www` flash partition (without it the hub still
runs, but serves a placeholder page).

```sh
cd web && npm install && npm run build && cd ..
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor   # the DevKitC's "UART" USB port
```

After that, updates can go over Wi‑Fi: Settings → Firmware → Choose file,
and pick `firmware/build/fanwave.bin`. (That updates the firmware only; the
web app's partition still needs `idf.py flash`.)

## First start

1. With no saved network the hub opens an access point named
   `FanWave-XXXX` (open by default; set a password under menuconfig →
   FanWave). The LED pulses blue.
2. Join it from your phone and open http://192.168.4.1. Pick your Wi‑Fi and
   enter the password.
3. The app shows the hub's new address on your network. Switch the phone
   back to your Wi‑Fi and open that address. The setup network closes two
   minutes later.

The hub also answers at http://fanwave.local (mDNS) and advertises itself
as an `_http._tcp` service with its `FanWave-XXXX` name in the `id` TXT
record. A second hub on the same network becomes `fanwave-2.local`. Some
Android browsers don't resolve `.local` names; use the address there.

If the saved network can't be reached for 20 seconds after boot, the
access point opens again so you can fix it, while the hub keeps retrying.

## Console

At the `fanwave>` prompt:

| Command | What it does |
|---|---|
| `status` | Radio found?, frequency, modulation, RSSI, edges/s on GDO2 (0 while listening means the receive wire is off), noise threshold |
| `freq 303.9` | Change frequency (MHz) |
| `mod ook` / `mod fsk` | Change modulation |
| `listen on` / `listen off` | Start or stop capturing |
| `scan` / `scan 430 437` / `scan 300 348 15` | Find a remote's frequency: stay still, then hold a button when asked (range in MHz, hold time in seconds) |
| `threshold` / `threshold -95` | Show or set the noise threshold for undecoded bursts; saved |
| `protocols` | List the decoders |
| `send pt2262 3A5C16 24 8` | Encode a code and transmit it (protocol, hex, bits, repeats) |
| `replay 3` | Retransmit the last capture exactly as recorded |
| `led off` / `led brightness 20` / `led identify` | Status LED |

Every capture is logged with its timing and any decoder matches:

```
I (5120) fanwave: capture: 100 marks, 98 ms, short 352 µs, long 1047 µs, gap 10871 µs, RSSI -52 dBm
I (5120) fanwave:   pt2262: 0x3A5C16 (24 bits, 4 repeats, base 349 µs, 95%)
```

Undecoded bursts weaker than the noise threshold are dropped, and logged so
they're not silent: up to five a second, then a count of the rest.

```
I (7311) fanwave: filtered as noise: 40 edges at -86 dBm (threshold -80 dBm)
I (8020) fanwave: filtered 12 more as noise in the last second (strongest -83 dBm)
```

If a remote never shows up, `scan` it first. If it's found but presses still
don't appear, compare its level with the threshold and lower it. Both are also
in the app, under RF Lab.

## Layout

| Path | |
|---|---|
| `components/cc1101` | SPI driver: reset, register setup per band, RX/TX/idle, RSSI |
| `components/rf` | RMT capture on GDO2 (DMA, 4096 edges) and replay on GDO0 |
| `components/protocol` | Fixed-code decoders and encoders; plain C |
| `components/status_led` | WS2812 driver on RMT, and the LED patterns (plain C) |
| `main/wifi.c` | Home network, setup access point fallback, scan and connect |
| `main/api.c` | HTTP API, `/api/events` WebSocket, OTA upload, the web app's files |
| `main/fans.c` | Rooms and fans in `/data`, pairing, which button a command sends, remote-press confirmation |
| `main/captures.c` | The last 20 captures and their decodes |
| `main` | Start-up, capture loop, serial console |
| `test/host` | Host tests for `protocol` and the LED patterns |

Run the host tests (no board or ESP-IDF needed):

```sh
cmake -S test/host -B build-host && cmake --build build-host && ctest --test-dir build-host
```
