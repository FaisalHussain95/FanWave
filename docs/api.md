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
| `GET /api/receiver` | `{listening, modulation, frequencyMHz}` |
| `POST /api/receiver` | Any of `{listening, modulation, frequencyMHz}`. Returns the new state |

Bursts that no decoder recognises and that arrive weaker than
`CONFIG_FANWAVE_RF_MIN_RSSI` (−80 dBm by default) are treated as noise and
never become captures.

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
```
