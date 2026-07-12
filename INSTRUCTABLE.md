# EyeD Badge — a Wearable LED Matrix Badge That Watches Back

> Draft for instructables.com — each `## Step` below becomes one step on the site.
> `[PHOTO: ...]` notes tell you what picture/video to attach there.

---

## Intro

Meet the **EyeD Badge** — a wearable 8×16 RGB LED badge powered by an ESP32-C6.
When it boots, a single Big Brother–style eye stares out, wanders, and blinks at
people around you. From any phone or laptop you can connect over Bluetooth —
no app to install, just a web page — and make it scroll messages, play 15+
animations (including Nyan Cat with a waving rainbow trail), or even **draw
pixel art on it live, pixel by pixel, in any color**.

Everything is open source: firmware + control page live in one repo:
**https://github.com/AahanDoesGit/Badge**

Features:
- Big Brother eye idle animation (almond outline, wandering red pupil, random blinks)
- Text scrolls vertically like falling letters — built for wearing on a lanyard
- 15+ animations: matrix rain, fire, plasma, Game of Life, equalizer, radar, comet, starfield, Nyan Cat...
- Control from a web page over Bluetooth LE (Web Bluetooth — Chrome on Android/desktop, Bluefy on iPhone/iPad)
- Live draw mode: tap the grid on the web page and the same pixel lights on the badge instantly
- Brightness and color control, with a power cap so it's battery-friendly

[PHOTO: hero shot — badge hanging on lanyard with the eye visible]
[VIDEO: 15–30s clip cycling: eye → text scroll → nyan cat → drawing from phone]

---

## Supplies

**Electronics**
- ESP32-C6 dev board (any "ESP32-C6 Dev Module" compatible board)
- 8×16 WS2812B RGB LED matrix panel (128 LEDs; sold as "RGB Matrix 8x16")
- 3 jumper wires (5V, GND, data)
- Optional but recommended: 330–470 Ω resistor in series with the data line
- Optional: 1000 µF capacitor across 5V/GND at the panel (protects the LEDs)
- USB cable / 5V power bank or LiPo battery for wearing it

**Other**
- Lanyard + 3D-printed case or frame (or double-sided tape and courage)
- Computer with Arduino IDE 2.x

**Cost:** roughly $10–15 depending on where you buy the panel and board.

[PHOTO: all parts laid out on a table]

---

## Step 1: How It Works

Three pieces make the badge tick:

1. **The panel** — 128 WS2812B LEDs in an 8×16 grid. One data wire, daisy-chained.
2. **The ESP32-C6** — runs the firmware: draws animations with FastLED and
   listens for commands over Bluetooth LE (Nordic UART Service, so even a
   generic BLE terminal app can drive it).
3. **The web page** — a single HTML file hosted free on GitHub Pages. It uses
   the Web Bluetooth API to talk straight from the browser to the badge. It
   also shows a live simulated preview of the panel, so you see what the badge
   is showing without looking down at your own chest.

The badge is worn in **portrait** (8 wide, 16 tall), so the firmware maps a
portrait canvas onto the landscape-wired panel and everything — text, rain,
fire — falls the right way when it's hanging from your neck.

[PHOTO: simple diagram — phone → BLE → ESP32-C6 → data wire → matrix]

---

## Step 2: Wire It Up

Only three wires:

| Matrix pin | ESP32-C6 pin |
|------------|--------------|
| VCC        | 5V (VBUS)    |
| GND        | GND          |
| DIN        | GPIO0        |

Tips learned the hard way:
- **Power the panel from 5V, not 3.3V.** At low voltage some LEDs "ghost glow"
  a faint blue even when they're told to be off.
- Put a **330–470 Ω resistor** in the data line right at the panel input. The
  ESP32's 3.3V data into a 5V panel is borderline; the resistor stops random
  glitched pixels (I once had a lone blue pixel latch itself on — this fixes that).
- If you use a different GPIO for data, change one line in the firmware
  (`#define DATA_PIN`).

The firmware caps LED power at 5V/1500mA (`FastLED.setMaxPowerInVoltsAndMilliamps`),
so a cheap power bank handles it fine at wearable brightness.

[PHOTO: close-up of the three wires between board and panel]

---

## Step 3: Case and Lanyard (Optional)

I 3D printed a simple frame with a loop on top for the lanyard ring. Anything
works: a printed case, laser-cut acrylic sandwich, or even cardboard. Two things
matter:

- The panel hangs in **portrait** with the connector end at the bottom.
- A diffuser layer (thin white acrylic, paper, or printed 0.6 mm plate) makes
  the pixels look dramatically better.

[PHOTO: badge in its case, front and back]
[FILES: attach your case STL here if you have one]

---

## Step 4: Set Up the Arduino IDE

1. Install **Arduino IDE 2.x**.
2. Boards Manager → install **esp32 by Espressif** (version 3.1 or newer — the
   C6 needs the newer core).
3. Library Manager → install **FastLED** (3.7 or newer).
4. Select board: **ESP32C6 Dev Module**, and pick your serial port.

[PHOTO: screenshot of Boards Manager with esp32 core installed]

---

## Step 5: Flash the Firmware

1. Grab the code: https://github.com/AahanDoesGit/Badge — you need
   `badge_firmware.ino`.
2. Open it in Arduino IDE. At the top, personalize:

```cpp
#define DATA_PIN      0         // GPIO your matrix data is on
#define SERPENTINE    false     // see Step 6 if your text looks scrambled
#define ROTATE_180    false     // flip if everything is upside-down
#define NAME_TEXT     "Aahans"  // your name for name-scroll mode
```

   And your Bluetooth name in `setupBLE()`:

```cpp
BLEDevice::init("EyeD Badge");
```

3. Upload. The badge boots straight into the idle eye.

[PHOTO: the eye running for the first time]

---

## Step 6: Match the Firmware to YOUR Panel

Not all 8×16 panels are wired the same, and this is where every build differs.
Three switches at the top of the firmware handle all of it — no other code
changes needed:

- **Text splits and comes in from both sides at once?** Your panel's rows all
  run the same direction (progressive) but the firmware expects zigzag — or
  vice versa. Flip `SERPENTINE`.
- **Everything is upside-down / rain "falls" toward the lanyard?** Flip
  `ROTATE_180`.
- **Panel completely dark but Bluetooth connects?** Wrong `DATA_PIN` — set it
  to the GPIO you actually wired.

Flash, look, flip, flash again. It takes two minutes and then it's perfect
forever.

[VIDEO: optional — clip of the "text from both sides" bug, it's a fun visual]

---

## Step 7: Host the Control Page

The controller is one HTML file (`index.html`). Host it anywhere HTTPS —
GitHub Pages is free:

1. Fork/clone the repo to your GitHub account.
2. Repo Settings → Pages → deploy from the `main` branch.
3. Your controller is now at `https://<you>.github.io/Badge/`.

Why HTTPS? Web Bluetooth only runs in secure contexts. (Opening the file
locally in Chrome also works for testing.)

**Browser support:** Chrome/Edge on Android, Windows, Mac, Linux. On
iPhone/iPad, Safari doesn't do Web Bluetooth — use the free **Bluefy** browser
app and open the same URL there.

[PHOTO: screenshot of the control page in a phone browser]

---

## Step 8: Connect and Play

1. Open your page, tap **connect**, pick "EyeD Badge" from the scan list.
2. The page unlocks:
   - **text** — type anything, it falls down the badge letter by letter
   - **modes** — eye, rain, plasma, fire, sparkle, shapes, Game of Life,
     bounce, equalizer, radar, comet, stars, pulse, **nyan**, or auto-cycle
   - **brightness** slider and **color** swatches

Every command is a tiny text message over the Nordic UART Service — so if you
ever want to script the badge from a laptop or another microcontroller, it's
trivial: send `M:16` for Nyan Cat, `B:80` for brightness, `C:FF0044` for color,
or any plain text to scroll it.

[VIDEO: connecting and switching a few modes]

---

## Step 9: Draw Mode — Pixel Art, Live

My favorite part. Flip the **draw** slider on the web page:

- The preview grid becomes a canvas — tap or drag to paint.
- Every pixel you touch appears on the physical badge **instantly**.
- Pick any color per pixel (palette swatches or a full color picker).
- Tap a painted pixel again to erase it. The drawing stays on the badge until
  you switch modes.

Under the hood each touch sends a 10-character BLE message (`P:xyRRGGBB`), and
the firmware paints it straight into the frame buffer — fast enough to doodle
with a fingertip.

[VIDEO: drawing a heart or smiley from the phone, badge mirroring it live]

---

## Step 10: Troubleshooting

Everything that bit me, so it doesn't bite you:

- **One random pixel stuck on a wrong color** → power-cycle or switch modes to
  re-sync; if it recurs, add the 330–470 Ω data resistor (Step 2).
- **Faint blue glow on "off" LEDs** → panel undervolted; feed it real 5V.
- **Web page can't find the badge** → the page filters by Bluetooth name
  prefix. If you renamed the badge in firmware, update the `namePrefix` filter
  in `index.html` to match.
- **iPhone/iPad shows "web bluetooth unavailable"** → that's Apple; use Bluefy.
- **Text garbled/mirrored/upside-down** → Step 6 flags, always.

---

## Step 11: Wear It

Clip it on the lanyard and walk into any hackathon, meetup, or conference. The
eye does the ice-breaking for you — and when someone asks about it, hand them
your phone and let them draw on your chest. Instant friend.

Ideas to take it further:
- Battery + tiny charging board inside the case
- A "message received" mode triggered from a phone notification
- Sound-reactive equalizer with a small I2S mic
- Two badges that find each other over BLE and sync animations

If you build one, post a picture in the comments — I'd love to see it!

[PHOTO: closing shot — badge worn, eye mid-blink]
