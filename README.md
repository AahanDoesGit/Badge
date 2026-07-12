# EyeD Badge

A wearable 8×16 RGB LED matrix badge on an ESP32-C6. It boots into a single
Big Brother–style eye that wanders and blinks — and from any phone or laptop
you can connect over Bluetooth (no app, just a web page) to scroll messages,
play animations, or draw pixel art on it live.

**Live controller:** enable GitHub Pages on this repo and open
— works in Chrome (Android /
desktop) and the Bluefy app (iPhone / iPad).

## Features

- **The Eye** — almond outline, wandering red pupil, random blinks (boot mode)
- **Falling text** — send any message; letters drop down the badge in reading order
- **17 modes** — matrix rain, plasma, fire, sparkle, shapes, Game of Life,
  bounce, equalizer, radar, comet, stars, pulse, Nyan Cat, auto-cycle, and a
  live draw canvas
- **Draw mode** — paint the badge pixel-by-pixel from the web page, any color each
- **Web Bluetooth control** — brightness, color, mode, text; with a live LED
  preview that mirrors the badge
- **Open protocol** — plain-text commands over Nordic UART; drivable from any
  BLE terminal or script

## Repo layout

```
index.html                              ← the web controller (GitHub Pages serves this)
firmware/badge_firmware/badge_firmware.ino  ← the ESP32-C6 firmware (Arduino)
docs/                                   ← build guide & code walkthrough
```

## Quick start

### 1. Hardware (3 wires)

| Matrix | ESP32-C6 |
|--------|----------|
| VCC    | 5V       |
| GND    | GND      |
| DIN    | GPIO0    |

Recommended: 330–470 Ω resistor in the data line, 1000 µF cap across the
panel's power. Feed the panel real 5V — undervolting causes faint blue ghosting.

### 2. Firmware

- Arduino IDE with **esp32 core ≥ 3.1** and **FastLED ≥ 3.7**
- Board: *ESP32C6 Dev Module*
- Open `firmware/badge_firmware/badge_firmware.ino`, tune the flags at the top:

```cpp
#define DATA_PIN    0      // your data GPIO
#define SERPENTINE  false  // flip if text splits to both sides
#define ROTATE_180  false  // flip if everything is upside-down
#define NAME_TEXT   "YOURNAME"
```

- Upload. The eye should appear.

### 3. Control page

Fork → Settings → Pages → deploy `main`. Open the URL over HTTPS, tap
**connect**, pick *EyeD Badge*.

## Command protocol

Everything is a short text write to the NUS RX characteristic
(`6E400002-B5A3-F393-E0A9-E50E24DCCA9E`):

| Command      | Effect |
|--------------|--------|
| `M:0`–`M:17` | switch mode (1 = eye, 16 = nyan, 17 = draw canvas) |
| `B:1`–`B:255`| brightness |
| `C:RRGGBB`   | badge color |
| `P:xyRRGGBB` | draw one pixel (x, y as single hex digits) |
| `P:C`        | clear the draw canvas |
| any text     | scroll it |

## Docs

- [`docs/INSTRUCTABLE.md`](docs/INSTRUCTABLE.md) — full step-by-step build guide
- [`docs/CODE_EXPLAINED.md`](docs/CODE_EXPLAINED.md) — every function of both files explained

## License

MIT — see [LICENSE](LICENSE).
