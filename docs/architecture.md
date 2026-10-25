# Architecture

FanWave is split into four layers. Each one talks only to its neighbors.

## 1. Web app (PWA)

A static TypeScript/Svelte build served from the ESP32's flash.

- **Control:** fan list grouped by room, speed / power / light controls,
  favorites, last-used fan (stored in `localStorage`).
- **Pairing wizard:** guides the user through capturing each button of a remote
  and mapping it to a fan capability.
- **RF Lab:** live capture view, pulse-timing inspector, bitstream decoder,
  capture comparison, protocol match results.

Talks to the firmware over a REST-style HTTP API (configuration, commands) and
a WebSocket (live captures, status events).

## 2. Firmware core (ESP-IDF)

- HTTP server and WebSocket endpoint
- Wi-Fi station mode with access-point fallback for onboarding
- Configuration storage (NVS or a flash filesystem) for fans, rooms, learned
  codes, and settings, with backup/restore as JSON
- OTA updates
- Status LED driver (see below)

### Status LED

The ESP32-S3-DevKitC-1 has one addressable RGB LED (WS2812-type). It is the
hub's only way to show what it's doing without opening the app.

**Wiring.** The LED's data pin is GPIO48 on v1.0 boards and GPIO38 on v1.1;
make it a build-time setting and check the board revision. It needs a timed
data signal, not a plain on/off pin: the firmware drives it with a small
WS2812 encoder on the RMT peripheral (`firmware/components/status_led`). On the N16R8 module the octal PSRAM takes
GPIO35–37, so the CC1101's SPI and data pins must stay off those and off the
LED pin.

**Patterns.** One state is shown at a time. A higher row overrides lower ones;
radio flashes play briefly on top of the base state and then return to it.

| Priority | Hub state | LED |
|---|---|---|
| 1 | Error: radio not found, update failed, storage full | Red, steady |
| 2 | Firmware update in progress | Purple, blinking; green flash when done |
| 3 | Learning a remote button | Breathing; green flash when the code is caught |
| 4 | Sending a command to a fan | Short flash, accent colour |
| 4 | Radio received a signal | Short flash, white |
| 5 | Wi‑Fi setup mode (own access point) | Slow blue pulse |
| 5 | Connecting to Wi‑Fi | Fast blue blink |
| 5 | Online and idle | Dim steady accent, or off (setting) |

The send and receive flashes are the in-room version of the app's
commanded-vs-confirmed distinction: a send flash means FanWave transmitted,
a receive flash means it heard a remote.

**Settings.** Stored with the hub's other settings and exposed in the web
app's Settings screen:

```jsonc
{
  "led": {
    "enabled": true,        // false turns the LED off except for errors
    "brightness": 40,       // 0–100
    "idle": "dim",          // "dim" | "off": what to show when online and idle
    "radioFlashes": true    // flash on send and receive
  }
}
```

The Settings screen should also offer **Identify**: the LED blinks for a few
seconds so you can tell which board you're talking to.

## 3. RF engine

Drives the CC1101 over SPI. Wiring is in [`firmware/README.md`](../firmware/README.md).

- **RX:** asynchronous (raw) mode for capturing pulse timings; packet mode where
  a protocol is known
- **TX:** replays learned raw timings or encoded frames
- Configurable modulation (OOK / 2-FSK), frequency, data rate, and bandwidth.
  Many US fan remotes use 302–305 MHz rather than 433.92; see
  [protocols.md](protocols.md).

## 4. Protocol engine

Which protocols, and why: [protocols.md](protocols.md).

- **Decode:** turn pulse timings into bitstreams
- **Match:** compare against known fan-remote protocols and report frame
  structure, address, command, and a confidence score
- **Encode:** build frames for any command on a known protocol, so a fan can be
  controlled without learning every button
- **Raw fallback:** store and replay timings when no decoder matches

## Fan model

```jsonc
{
  "id": "living-room",
  "name": "Living Room",
  "room": "Living room",
  "protocol": "raw",            // or a decoder id
  "address": null,              // protocol-specific remote address
  "capabilities": ["power", "speed", "light", "reverse", "timer"],
  "speeds": 6,
  "commands": { "power": { /* frame or raw timings */ } },
  "state": {
    "commanded": { "power": true, "speed": 3 },
    "confirmed": null           // unknown unless observed over RF
  }
}
```

The `commanded` vs. `confirmed` split exists because the physical remote can
change a fan's state without the hub being involved. When the hub hears a
matching frame from a remote, it can update `confirmed`.
