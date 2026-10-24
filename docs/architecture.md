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

## 3. RF engine

Drives the CC1101 over SPI.

- **RX:** asynchronous (raw) mode for capturing pulse timings; packet mode where
  a protocol is known
- **TX:** replays learned raw timings or encoded frames
- Configurable modulation (OOK / 2-FSK), frequency, data rate, and bandwidth

## 4. Protocol engine

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
