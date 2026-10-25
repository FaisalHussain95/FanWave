# Hub API

The firmware serves the web app and this API on port 80. Everything is JSON
under `/api`, plus one WebSocket for live events. The web app's client is
`web/src/lib/http.ts`; the shapes match `web/src/lib/types.ts`.

There is no authentication: the hub is meant for a trusted home network, and
the setup access point is only up while the hub has no working Wi‑Fi. Cross-
origin requests are not allowed, so other websites can't drive the hub from
a visitor's browser.

Times from the hub (`at`, `lastSentAt`) are milliseconds since the hub
booted; it has no clock. Every API response carries an `X-Hub-Now` header on
the same clock (capture lists and events also carry `now`), so clients
convert with `Date.now() - (now - at)`.

Errors are `{"error": "message"}` with a 4xx/5xx status.

## Hub

| | |
|---|---|
| `GET /api/hub` | `HubInfo`: name, hardware, address, uptime, Wi‑Fi, `setupMode` (its access point is up), heap, radio, LED settings, firmware. `update` is always `null`: there's no update server, so firmware is uploaded |
| `POST /api/led` | Body: part of `LedSettings`. Returns the stored `LedSettings` |
| `POST /api/identify` | Blink the LED for 5 s. `204` |
| `POST /api/ota` | Body: a firmware `.bin` (raw bytes, not multipart). Writes the inactive OTA slot, then reboots into it. `200 {"version"}` before rebooting |

## Wi‑Fi

| | |
|---|---|
| `GET /api/wifi/scan` | `WifiNetwork[]`, strongest first |
| `POST /api/wifi` | `{ssid, password}`. Connects, waiting up to 15 s. `200 {"address": "192.168.1.23"}`, or `400 {"error"}` with the reason (wrong password, not found…). The setup access point stays up for two more minutes so the phone can read the answer |

## Rooms and fans

| | |
|---|---|
| `GET /api/rooms` | `Room[]` |
| `POST /api/rooms` | `{name}` → `Room` |
| `GET /api/fans` | `Fan[]` |
| `POST /api/fans` | `NewFan` (below) → `Fan` |
| `POST /api/fans/{id}/command` | Part of `FanState`, e.g. `{"power": true, "speed": 3}`. Transmits the matching learned button and returns the updated `Fan` |

`NewFan` is the pairing result:

```jsonc
{
  "name": "Kitchen", "roomId": "living", "kind": "Ceiling fan",
  "protocol": "pt2262",          // or null for raw codes
  "address": "0x3A5C1",
  "slots": [
    { "key": "power", "label": "Power", "capability": "power",
      "status": "done", "code": "0x3A5C1 · 0110", "captureId": 41 },
    { "key": "speed-1", "label": "Speed 1", "capability": "speed",
      "status": "skipped", "code": null, "captureId": null }
  ]
}
```

The hub looks up each `captureId` among its recent captures (the last 20)
and stores either the decoded frame (protocol, bits, base pulse) or the raw
timings. The fan is stored with the frequency and modulation the receiver
was on while pairing.

Which button a command sends:

| Patch | Button |
|---|---|
| `{"power": true, "speed": n}` or `{"speed": n}` | `speed-n`, else `power` |
| `{"power": true}` while off | `speed-<last speed>`, else `power` |
| `{"power": false}` while on | `power` |
| `{"light": …}`, `{"reverse": …}`, `{"timer": …}` | `light`, `reverse`, `timer` |

Toggles are only sent when the value changes. `commanded` records what was sent. When the hub hears a remote send one of a
fan's learned codes, it updates `confirmed` and pushes a `fan` event.

## Radio

| | |
|---|---|
| `GET /api/captures` | `{now, captures: Capture[]}`, newest first, up to 20 |
| `GET /api/captures/{id}/analysis` | `ProtocolMatch[]`, best first; empty when nothing decodes |
| `GET /api/receiver` | `{listening, modulation, frequencyMHz, minRssiDbm, scanning, edgesPerSecond}`. `edgesPerSecond` counts level changes on the receive pin (GDO2) over the last second; noise alone keeps it well above 0 while listening, so a steady 0 means the pin is not connected |
| `POST /api/receiver` | Any of `{listening, modulation, frequencyMHz, minRssiDbm}`. Returns the new state. `409` if a scan is running and the radio settings would change |
| `POST /api/receiver/scan` | Start a frequency scan: `{fromMHz?, toMHz?, quietSeconds?, holdSeconds?}` (defaults 300, 464, 3, 9). `202` with the scan's status; `409` if one is already running |
| `GET /api/receiver/scan` | The current or last scan's status, with `spectrum` once done |
| `POST /api/receiver/scan/cancel` | Stop a running scan. `204` |

### How captures are cut

The receiver streams: the CC1101 turns background noise into a constant run
of edges on GDO2, so waiting for silence to end a capture would wait forever.
The firmware receives continuously and cuts the stream into captures itself:
at 20 ms of silence, or once a capture reaches 4096 edges or 300 ms. A held
button therefore arrives as several captures, each with whole frames to
decode, and noise arrives as weak, undecodable captures that the threshold
below drops.

### Noise threshold

Bursts that no decoder recognises and that arrive weaker than `minRssiDbm`
are treated as noise and never become captures. It starts at
`CONFIG_FANWAVE_RF_MIN_RSSI` (−80 dBm) and can be set from −110 to −30; a value
set through the API or the `threshold` console command is saved and kept
across restarts and updates. Dropped bursts are logged on the console, up to
five a second plus a count of the rest, and summarised in `filtered` events.

### Frequency scan

Finds what a remote transmits on. The hub steps the CC1101 across the range
(250 kHz steps for wide ranges, down to 50 kHz for narrow ones, skipping the
gaps between its bands) and keeps the loudest RSSI at each step, in two
passes: `quiet` with nothing pressed, then `hold` while the user holds a
button. The answer is the step that rose most over its own quiet reading, so
the CC1101's spurious responses at multiples of its 26 MHz crystal (390, 416,
442 MHz…) and the hub's own interference, which are loud in both passes, don't
win. If something rose 10 dB or more, a `refine` pass in 25 kHz steps around it
finds the centre. Captures, transmits and radio changes wait until the scan ends;
the receiver then returns to its previous frequency.

```jsonc
{
  "state": "done",            // idle | quiet | hold | refine | done | failed | cancelled
  "progress": 1,
  "fromMHz": 300, "toMHz": 464, "stepKHz": 250,
  "candidateMHz": 434.0,      // refine only
  "found": true,              // something rose 10 dB or more
  "frequencyMHz": 433.915,    // refined centre
  "peakDbm": -44, "riseDb": 55, "floorDbm": -99,
  "candidates": [{ "frequencyMHz": 434.0, "peakDbm": -48, "riseDb": 51 }],
  "spectrum": [[300.0, -100, -99], [300.5, -101, -100]],  // [MHz, quiet dBm, held dBm], up to 240 points
  "error": "…"                // failed only
}
```

## Backup

| | |
|---|---|
| `GET /api/backup` | `{version: 1, rooms, fans}`, including learned codes |
| `POST /api/restore` | The same document. Replaces rooms and fans |

## Events: `GET /api/events` (WebSocket)

Server to client only, one JSON object per text frame:

```jsonc
{ "type": "capture", "now": 123456, "capture": { /* Capture */ }, "matches": [ /* ProtocolMatch */ ] }
{ "type": "fan", "fan": { /* Fan */ } }
{ "type": "scan", "scan": { /* scan status, as above; spectrum only in the final one */ } }
{ "type": "filtered", "count": 12, "strongestDbm": -83, "thresholdDbm": -80, "periodMs": 2000 }
```

`scan` arrives on every phase change and about every 400 ms in between.
`filtered` summarises the bursts dropped as noise, every 2 s while there are any.
