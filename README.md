# FanWave

**Open-source 433 MHz smart fan hub.** Your home, your breeze.

FanWave is ESP32-S3 and CC1101-powered firmware for discovering, decoding,
learning, and transmitting RF ceiling fan remote commands. It ships a
self-hosted, mobile-first Progressive Web App for pairing and managing multiple
fans, organizing them by room, controlling speed and power, and exploring
reverse-engineered RF protocols.

No cloud, no proprietary hub — just local Wi-Fi and radio.

> **Status:** the web app (`web/`) runs against a mock hub. The firmware
> (`firmware/`) brings up the radio and captures, decodes and replays remote
> codes from a serial console; Wi‑Fi and the API the app needs come next.
> None of it has run on real hardware yet.

## Web app

The PWA lives in [`web/`](web/): Svelte 5, TypeScript and Vite, styled with
the Nocturne design system from the FanWave Claude Design project.

```sh
cd web
npm install
npm run dev     # local dev server
npm run check   # type-check
npm run build   # static build in web/dist, ready to serve from flash
```

It has nine screens: Wi‑Fi onboarding, the dashboard, single-fan control,
pairing (listen for a remote, then learn each button), rooms, hub settings,
the RF Explorer and Protocol Detective. Everything goes through the `HubApi`
interface in `web/src/lib/api.ts`; `MockHub` in `web/src/lib/mock.ts` stands
in for the firmware and keeps its data in `localStorage`. Swap it for an
HTTP/WebSocket client once the firmware API exists.

## Firmware

ESP-IDF 5.4 project in [`firmware/`](firmware/): wiring, build and console
commands are in its [README](firmware/README.md). Supported remote protocols
and the research behind them are in [docs/protocols.md](docs/protocols.md).

## Two experiences

- **Control** — the everyday dashboard: pick a fan, set its speed, toggle the
  light, turn it off. Built for household users.
- **RF Lab** — the workbench: capture remotes, inspect pulse timings, and
  identify or learn protocols. Built for pairing new fans and development.

## Planned features

### RF Explorer
Capture transmissions, inspect pulse timings, switch between OOK and FSK, view
decoded bitstreams, and compare repeated button presses.

### Protocol Detective
Compare captures against known reverse-engineered protocols. Show possible
matches, frame structure, address fields, commands, and confidence levels.
Fall back to raw-code learning when no decoder matches.

### Fan Manager
Add or remove fans and assign names, rooms, remote addresses, and supported
controls. Learn power, speed, reverse, light, timer, and other buttons
according to each fan's capabilities.

### Mobile-first PWA
Installable on phones with a home-screen shortcut, a fast control dashboard,
favorite fans, a remembered last-used fan, dark mode, and optional haptic
feedback.

### Device management
Wi-Fi onboarding with access-point fallback, OTA updates, RF diagnostics,
settings backup and restore, and fully local operation.

## Architecture

```
┌──────────────────────────────────────────────┐
│ Mobile-first PWA                             │
│ Fan dashboard · Pairing wizard · RF Lab      │
└──────────────────────┬───────────────────────┘
                       │ HTTP API · WebSocket
┌──────────────────────▼───────────────────────┐
│ ESP32-S3 firmware                            │
│ HTTP API · WebSocket · Configuration storage │
├──────────────────────┬───────────────────────┤
│ RF engine            │ Protocol engine       │
│ CC1101 RX/TX         │ Decode · Match · Encode│
└──────────────────────┴───────────────────────┘
                       │ 433 MHz
┌──────────────────────▼───────────────────────┐
│ Fans and remotes                             │
│ Multiple devices · Multiple protocols        │
└──────────────────────────────────────────────┘
```

See [docs/architecture.md](docs/architecture.md) for details.

### Hardware

- ESP32-S3 (N16R8 — 16 MB flash, 8 MB PSRAM)
- CC1101 sub-GHz transceiver tuned for 433.92 MHz

### Software stack

- **Firmware:** ESP-IDF, C/C++
- **Web app:** TypeScript with a lightweight framework (Svelte), built to static
  assets and served from the ESP32's flash
- **Storage:** fan configuration persisted on the device; the last selected fan
  remembered in browser storage

## Things to know

- **433 MHz is the carrier band, not the protocol.** Fan remotes may use OOK or
  FSK, different timings, or proprietary framing. The CC1101 helps investigate
  them, but automatic identification of every remote isn't guaranteed.
- **State is commanded, not confirmed.** A physical remote can change a fan
  without the hub knowing, so the UI distinguishes the last commanded state
  from a confirmed one.
- Only transmit to devices you own, and follow your local radio regulations.

## References

- [ESPHome CC1101 component](https://esphome.io/components/cc1101/) —
  asynchronous and packet-based modes
- [esphome_rtl433](https://github.com/SilverCory/esphome_rtl433) — rtl_433
  decoders on an ESP32/CC1101 gateway
