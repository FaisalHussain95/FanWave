# Fan remote protocols

A survey of existing reverse-engineering work on ceiling-fan RF remotes, used
to decide which decoders FanWave implements. Compiled in October 2026 from the
sources linked below. Timings are as each source states them; where a source
is vague or contradicts another, that's noted. None of it has been checked
against real remotes yet.

## What FanWave implements today

`firmware/components/protocol` has a table-driven decoder and encoder for
rc-switch-style fixed codes: every symbol is one mark and one space in small
multiples of a base pulse, and frames are separated by a sync symbol. Rows:

| id | Covers | Base | Sync | 0 / 1 | Bits |
|---|---|---|---|---|---|
| `pt2262` | EV1527 / PT2262: generic Chinese 433 MHz remotes | 350 µs | 1:31 | 1:3 / 3:1 | 24 |
| `ht12e` | Holtek HT12E (inverted) | 270 µs | 36:1 | 1:2 / 2:1 | 12 |
| `sm5212` | SM5212 (inverted) | 320 µs | 36:1 | 1:2 / 2:1 | 12 |
| `dl-fanimation` | DL fan remotes as used by Fanimation (inverted) | 346 µs | 35:1 | 1:2 / 2:1 | 12–13 |
| `dl-hamptonbay` | Hampton Bay A25-TX028 / DawnSun (inverted) | 400 µs | 34:1 | 1:2 / 2:1 | 12–24 |
| `dl-fanhd` | "fan-hd 201946" (inverted) | 361 µs | 35:2 | 1:2 / 2:1 | 12–13 |
| `ht6p20b` | Holtek HT6P20B (inverted) | 450 µs | 23:1 | 1:2 / 2:1 | 24–28 |
| `rcs2`, `rcs3`, `rcs4`, `rcs5`, `hs2303` | Other rc-switch protocols | | | | |

Anything that doesn't decode is kept as a raw capture and replayed exactly.

## Frequency matters more than protocol

Many US ceiling-fan remotes (Hampton Bay, Harbor Breeze, Minka, Fanimation,
Craftmade) transmit at **about 302–305 MHz**, not 433.92. Remotes drift: one
source measured 303.4–304.7 MHz across units, and several projects had to
sweep ±1 MHz to find a frequency that worked. Two consequences:

- The CC1101 needs different calibration below 430.6 MHz (TEST0 = 0x0B,
  FSCAL2 = 0x0A, per frizzby/hampton-bay-fan-rf). The driver switches these
  automatically.
- A CC1101 module tuned for 433 MHz has poor range at 303 MHz. Use a 315 MHz
  module for those fans.

## Families found

| | Family / remotes | Freq, modulation | Timing | Frame | Code |
|---|---|---|---|---|---|
| A | **DL / Chungear 12–13-bit DIP** (HT12E-like): Harbor Breeze FAN-11T, FAN-53T, UC-9050T, UC-7080T; Hampton Bay UC7052T, UC7078T/TR; Minka RC120 (DL-4103T02); Fanimation DL-4114; Chungear BCF-0019x2; Craftmade | 302.4–304.95 MHz, OOK PWM | Base ≈ 320–400 µs, 1:2 / 2:1; frame gap ~10–12 ms (Chungear 252/484 µs) | Usually `01` + 4-bit DIP + 1 bit + 6-bit command, often one-hot and inverted. Minka (DIP 0000): Hi 0x860, Med 0x850, Low 0x848, Off 0x842, Light 0x841. UC7052T: Hi 011111, Med 101111, Low 110111, Light 111110, Off 111101, Rev 111011 | Fixed |
| B | **EV1527 / PT2262**: cheap Chinese fan and light remotes | 433.92 MHz OOK | 350 µs, 1:3, sync 1:31 (Flipper's Princeton: 390/1170 µs) | 20-bit ID + 4-bit key | Fixed |
| C | **UniFan-24V / XH-…-B99** DC fan controllers | 433.92 MHz OOK PWM | 756/252 µs (sources disagree on which is 0), 3616 µs start pulse, 7–10 repeats | 20-bit ID, 5-bit button, 3-bit counter, 4-bit XOR checksum | Counter (mod 8) |
| D | **Hampton Bay FAN-9T**, 21 bits | 303.631 MHz | rc-switch 6 (450 µs, inverted) | DIP (inverted, lookup table) + brightness + light + 2-bit speed + optional checksum | Fixed |
| E | **Hampton Bay A25-TX028 / TX028C-2** (DawnSun), 24 bits | 303.87–304.015 MHz | 400 µs (one project uses 384 µs) | `0xFC6` + 4-bit inverted DIP + `011` + 3-bit command + 2-bit data. Power on 0xFC607E, off 0x7D; fan off/low/med/high 0x77/76/75/74; light on/off 0x72/0x71 | Fixed |
| F | **Hampton Bay "Ashby Park"** (Bond), 25 bits | 304.25 MHz OOK PWM | 744 / 376 µs (labels in the source look swapped) | 20-bit ID + 2-bit speed | Fixed |
| G | **Fanimation KUJCE10711**, 25 bits | not stated, OOK PWM | 400/800 µs | `0x800` + 5-bit DIP + 8-bit command (16 speeds, light, timers) | Fixed |
| H | **Regency** | 303.75–303.96 MHz | 576 µs start; asymmetric symbols | 20 bits, nibble-sum checksum | Fixed |
| I | **Martec MPLCD** (Australia) | not stated | 292/648 µs | 22 bits, full state per frame, checksum | Fixed |
| J | **Honeywell "Salermo"** | 314.92 MHz OOK PPM | 300/900 µs | 24 bits; layout uncertain in source | Fixed |
| K | **Mercator FRM87GL** (Ikuü) | not stated, OOK PCM | 333 µs chips | 43 bits, one-hot button | Fixed |
| L | **Mantra R00149** (Spain) | 433 MHz OOK | 340/850 µs | 48 bits, full state, checksum | Fixed |
| M | **Hunter** (433 MHz) | 433 MHz OOK | 400 µs, long preamble | 65–66 bits, not decoded; replay only. Older Hunter remotes are 350 MHz HT12E; some may be rolling code | Unknown |

No usable documentation was found for Westinghouse, Lamparo, Lucci Air,
Brilliant, Casablanca, UC7067RY, Fan-35T, TR18x, AC-552 or MR101. Sonoff iFan
RM433 is 433.92 MHz and pairs by learning; that it's EV1527 is a guess. No
FSK fan remotes turned up.

## Next decoders, in order

1. Family A command helpers on top of the existing inverted rows: DIP and
   command fields, the key-release frame, and a 300–305 MHz frequency sweep
   during pairing.
2. UniFan-24V (C): needs a start-pulse header and the checksum/counter, so raw
   replay won't work for it.
3. Hampton Bay 21-bit (D) and 24-bit (E) field layouts.
4. Fanimation 25-bit (G) and Regency (H).
5. Stateful frames (Martec, Mantra), Mercator PCM, Honeywell.

Raw replay can't follow counter-based (C) or whole-state (I, L) protocols;
the app should say so when it falls back to raw for those.

## Sources and licenses

FanWave's code is written from the facts above, not copied. Timing and frame
layouts are facts; the code in these projects is under the licenses listed.

| Source | What | License |
|---|---|---|
| [merbanan/rtl_433](https://github.com/merbanan/rtl_433): `conf/fan-11t.conf`, `conf/FAN-53T.conf`, `conf/hamptonbay-ceilingfan.conf`, `conf/Fanimation-Fan.conf`, `conf/Mercator.conf`, `conf/honeywell-fan.conf`, `conf/chungear_bcf-0019x2.conf`, `src/devices/universalfanctrl.c`, `regency_fan.c`, `martec_mplcd.c` | Families A, C, F–K | GPL-2.0+ |
| [sui77/rc-switch](https://github.com/sui77/rc-switch) `RCSwitch.cpp`, and its fan forks (LSatan SmartRC, patrickdk77) | rc-switch protocol table, rows 11–15 | LGPL-2.1 |
| [flipperdevices/flipperzero-firmware](https://github.com/flipperdevices/flipperzero-firmware) `lib/subghz/protocols` | Princeton and other fixed codes | GPL-3.0 |
| [esphome/esphome](https://github.com/esphome/esphome) `remote_base/rc_switch_protocol.h` | rc-switch rows 1–8 | GPL-3.0 (C++) |
| [frizzby/hampton-bay-fan-rf](https://github.com/frizzby/hampton-bay-fan-rf) `docs/protocol.md` | Family A, CC1101 calibration below 430 MHz | MIT |
| [spectrumjade/hass-minka-ceiling-fan](https://github.com/spectrumjade/hass-minka-ceiling-fan) `remotes/DL-4103T02.md` | Minka codes | MIT |
| [patrickdk77/hampton-bay-fan-mqtt](https://github.com/patrickdk77/hampton-bay-fan-mqtt) `rf-fans/` | Families A, D, E | none stated |
| [owenb321/hampton-bay-fan-mqtt](https://github.com/owenb321/hampton-bay-fan-mqtt) | Family D | none stated |
| [spetryjohnson/cc1101-mqtt-fan-controller](https://github.com/spetryjohnson/cc1101-mqtt-fan-controller) | Family E | Unlicense |
| [bforejt/esphome-cc1101-ceiling-fan](https://github.com/bforejt/esphome-cc1101-ceiling-fan) | Family C | Apache-2.0 |
| [berfenger/esphome-mantra-rf-433](https://github.com/berfenger/esphome-mantra-rf-433) | Family L | MIT |
| [breandan81/HunterFan](https://github.com/breandan81/HunterFan) | Family M | MIT |
| [jakabo27/ESPHome-Craftmade-RF-Gateway](https://github.com/jakabo27/ESPHome-Craftmade-RF-Gateway) | Craftmade (A) | MIT |
| [ngutman/star-fan-control](https://github.com/ngutman/star-fan-control) | Star (Israel) fans | not checked |
