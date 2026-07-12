# EyeD Badge — Full Code Walkthrough

Two files make the whole project:

- **`badge_firmware.ino`** — runs on the ESP32-C6, drives the LEDs, listens on Bluetooth
- **`index.html`** — the control page: connects over Web Bluetooth and simulates the panel on screen

This document goes through both, part by part.

---

# PART 1 — The Firmware (`badge_firmware.ino`)

## 1.1 Configuration defines

```cpp
#define DATA_PIN      0     // GPIO the matrix data wire is on
#define WIDTH         8     // logical width, as worn (portrait)
#define HEIGHT        16    // logical height, as worn
#define PANEL_W       16    // how the panel is physically wired
#define PANEL_H       8
#define NUM_LEDS      (PANEL_W * PANEL_H)   // 128
#define SERPENTINE    false // true if odd rows are wired right-to-left
#define ROTATE_180    false // flip everything if mounted upside-down
#define NAME_TEXT     "Aahans"  // what "name" mode scrolls
#define SCROLL_MS     70    // ms per scroll step (lower = faster)
#define EYES_DWELL_MS 8000  // how long eyes show between name scrolls
```

The key idea: the panel is **physically** a 16-wide × 8-tall strip chain, but the
badge is **worn** vertically. So all drawing code thinks in a *logical* 8×16
portrait canvas, and one function (`XY`, below) translates to the physical wiring.
Change hardware details here — never in the animation code.

```cpp
CRGB leds[NUM_LEDS];      // the frame buffer: 128 RGB pixels FastLED pushes out
CRGB fgColor = ...;        // current badge color (changed by C: command)
uint8_t brightness = 30;   // global brightness (changed by B: command)
```

## 1.2 Mode state

```cpp
enum Mode { MODE_NAME, MODE_EYES, MODE_TEXT, MODE_RAIN, ... MODE_NYAN, MODE_DRAW };
volatile Mode mode = MODE_EYES;   // badge boots into the eye
String bleText = "";              // last text received over BLE
volatile bool newBleMsg = false;  // flag: "a BLE message is waiting"
String bleBuf = "";               // the waiting message itself
```

`volatile` matters: these are written from the Bluetooth callback (which runs on
a different task) and read in `loop()`. It tells the compiler "this can change
behind your back, don't cache it."

The enum numbers are exactly the numbers the web page sends (`M:16` = index 16 =
`MODE_NYAN`).

## 1.3 `XY(x, y)` — the coordinate translator

The most important 10 lines in the file. Everything draws in portrait
coordinates; this converts one logical (x, y) into the index of the physical LED
in the chain:

```cpp
uint16_t XY(int8_t x, int8_t y) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return NUM_LEDS; // off-grid guard
#if ROTATE_180
  x = WIDTH - 1 - x;  y = HEIGHT - 1 - y;   // optional 180° flip
#endif
  int8_t px = HEIGHT - 1 - y;   // rotate portrait 90° onto the landscape panel
  int8_t py = x;
  if (SERPENTINE && (py & 1))                    // zigzag panels: odd rows reversed
    return py * PANEL_W + (PANEL_W - 1 - px);
  return py * PANEL_W + px;                      // straight-wired panels
}
```

- Off-grid coordinates return `NUM_LEDS` (an index just past the buffer), which
  the next function safely rejects — so animations can draw partially off the
  edge without crashing.
- The 90° rotation is why text can "fall down" a panel that's electrically
  sideways.

## 1.4 `setPx(x, y, color)`

```cpp
void setPx(int8_t x, int8_t y, CRGB c) {
  uint16_t i = XY(x, y);
  if (i < NUM_LEDS) leds[i] = c;
}
```

The only safe way to light a pixel: translate coordinates, check the guard,
write into the frame buffer. Nothing appears on the physical LEDs until
someone calls `FastLED.show()`.

## 1.5 The font — `font5x7[]` and `fontCol()`

`font5x7` is a table of 5-byte entries, one per ASCII character from space to
'Z'. Each byte is one **column** of the character; each bit in the byte is one
**row** (bit 0 = top). So the letter A is 5 columns × 7 rows of on/off bits.

```cpp
uint8_t fontCol(char c, uint8_t col) {
  if (c >= 'a' && c <= 'z') c -= 32;   // lowercase → uppercase
  if (c < 32 || c > 90) c = '?';       // anything unknown prints as ?
  return pgm_read_byte(&font5x7[c - 32][col]);
}
```

`PROGMEM` / `pgm_read_byte` keep the table in flash instead of RAM.

## 1.6 The scroller — `drawScrollFrame()`

Draws one frame of vertically-falling text and advances it by one pixel.

```cpp
int scrollOffset;                                  // how far the fall has gone
int textPixelHeight(const String &s) { return s.length() * 8; } // 7px letter + 1 gap

bool drawScrollFrame(const String &s) {
  FastLED.clear();
  for (uint16_t ci = 0; ci < s.length(); ci++) {
    int cy = scrollOffset - (ci + 1) * 8;          // char 0 leads the fall
    if (cy <= -8 || cy >= HEIGHT) continue;        // skip chars not on screen
    for (uint8_t col = 0; col < 5; col++) {
      uint8_t bits = fontCol(s[ci], col);
      for (uint8_t row = 0; row < 7; row++)
        if (bits & (1 << row)) setPx(1 + col, cy + row, fgColor);
    }
  }
  FastLED.show();
  scrollOffset++;
  if (scrollOffset > textPixelHeight(s) + HEIGHT) { scrollOffset = 0; return true; }
  return false;
}
```

- `cy = scrollOffset - (ci+1)*8` is the trick that makes the **first** letter
  of the message enter first and lead the fall, with each next letter stacked
  above it — so you read the message in order as it drops.
- Letters are drawn at `x = 1..5`, centered in the 8-pixel width.
- Returns `true` when one full pass finished — name mode uses that to switch
  back to the eyes.

## 1.7 The eye — `drawEyesFrame()`

The idle animation: one Big Brother eye. Three little state machines run at once:

```cpp
int8_t pupilX, pupilTargetX;        // where the pupil is / wants to be (-1..1)
uint8_t blinkPhase;                 // 0 = open, 1..12 = mid-blink frame counter
uint32_t nextBlinkAt, nextLookAt;   // when to blink / glance next
```

Each frame:
1. **Blink scheduler** — if the time has passed `nextBlinkAt`, start a blink.
   `blinkPhase` counts 1→12: openness goes 6→0 (closing) then 0→6 (opening),
   then a new random blink time 1.5–5.5 s away is chosen.
2. **Glance scheduler** — every 0.9–3.1 s pick a new `pupilTargetX` (-1, 0 or 1);
   the pupil steps one pixel per frame toward it (smooth movement, not teleporting).
3. **Draw the almond outline** with an ellipse equation:

```cpp
const float cx = 3.5f, cy = 7.5f, a = 4.0f;   // center and horizontal radius
float b = 0.6f + 3.2f * openness / 6.0f;      // vertical radius follows the blink
...
float d = dx*dx + dy*dy;                       // normalized distance from center
if (d <= 1.0f && d >= 0.45f) setPx(x, y, fgColor);
```

   Points with `d ≤ 1` are inside the ellipse; requiring `d ≥ 0.45` keeps only a
   ring — the outline. Shrinking `b` squashes the ellipse flat = the eyelid closing.

4. **Iris + pupil** — a rounded 4×4 patch of dimmed `fgColor`
   (`iris.nscale8_video(80)` ≈ 30% brightness) around a 2×2 **red** pupil,
   drawn only when the eye is open enough (`openness >= 3`).

## 1.8 The animations

Each one draws a single frame; `loop()` calls it over and over. Common tricks:

- `fadeToBlackBy(leds, NUM_LEDS, n)` — dims the whole frame a step; drawing on
  top of that every frame creates motion trails "for free."
- `sin8/beatsin8/inoise8` — FastLED's fast integer wave/noise generators, used
  instead of slow floating point.

**Rain (`drawRainFrame`)** — one "drop head" per column (`rainHead[x]`). Each
frame: fade everything, draw the head white and the pixel behind it in
`fgColor`, move the head down. Past the bottom, the head is set to a *negative*
number = an off-screen countdown, so columns restart at random staggered times.

**Plasma (`drawPlasmaFrame`)** — for each pixel, add three sine waves of
position and time, use the sum as a hue → flowing color blobs. No state at all;
it's pure math of `millis()`.

**Fire (`drawFireFrame`)** — the classic heat simulation. A `heat[x][y]` grid:
every cell cools a random bit, heat drifts upward (each cell becomes a weighted
average of the cells *below* it), random sparks ignite at the bottom row, and
`HeatColor()` maps heat 0-255 to black→red→orange→white.

**Sparkle (`drawSparkleFrame`)** — fade, then with some probability drop one
`fgColor` dot and occasionally a white one at random positions. Glitter.

**Shapes (`drawShapesFrame`)** — every 0.3–0.9 s stamp a random outline
(rectangle / cross / diamond) at a random spot in a random hue; the global fade
melts them away.

**Game of Life (`drawLifeFrame`)** — real Conway rules on a wrap-around grid:
count each cell's 8 neighbors, live cell survives with 2-3, empty cell births
with exactly 3. Reseeds automatically when the population dies out (<3) or
after 120 generations (to escape frozen patterns). Dying cells leave fading
trails because the frame only half-fades.

**Bounce (`drawBounceFrame`)** — two balls with float positions and velocities;
each frame add velocity, flip its sign on a wall hit. One ball white, one
`fgColor`, both leaving trails.

**Equalizer (`drawEqFrame`)** — per column, `inoise8` (smooth random noise)
picks a target bar height; the bar moves one pixel per frame toward it (that
easing is what makes it look like music). Color by height: green base, amber
mids, red peaks.

**Radar (`drawRadarFrame`)** — up to 3 "ripples". Each has a center and a
radius that grows every frame; pixels whose squared distance falls between
`(r-1)²` and `r²` form the expanding ring.

**Comet (`drawCometFrame`)** — a full-height bar sweeping side to side on
`beatsin8` (a smooth sine bounce), hue slowly rotating, trails from the fade.

**Stars (`drawStarsFrame`)** — 14 stars, each with position, brightness, and a
brightness *velocity*. Rise to max → start dimming; hit zero → respawn at a new
random spot. Slightly blue-tinted white.

**Pulse (`drawPulseFrame`)** — the whole panel breathes: `beatsin8(16, 8, 160)`
gives a slow sine between dim and bright, applied to `fgColor`.

**Nyan (`drawNyanFrame`)** — a hand-drawn 8×9 sprite in a `PROGMEM` table where
each cell is a palette code (0=off, 1=gray head, 2=eye, 3=cheek, 4=crust,
5=frosting, 6=sprinkle). Below it, six vertical rainbow stripes; each row of
the trail gets a -1/0/+1 sideways offset from `nyanWob()`:

```cpp
int8_t nyanWob(uint32_t t, uint8_t y) {
  uint8_t s = sin8((uint8_t)(t / 6 + y * 32));   // a sine over time, phase-shifted per row
  return (s > 170) ? 1 : (s < 86 ? -1 : 0);      // quantized to -1 / 0 / +1
}
```

Because the phase depends on `y`, the offset ripples down the trail — the wave.
The cat itself uses the offset of the top trail row, so it bobs in sync.
Sprinkles swap color on a 250 ms timer = twinkling.

## 1.9 Bluetooth — the whole BLE section

The badge speaks **Nordic UART Service (NUS)** — a de-facto standard "serial
port over BLE" with fixed UUIDs. Any BLE terminal app can talk to it, not just
our web page.

```cpp
#define NUS_SERVICE "6E400001-..."   // the service
#define NUS_RX      "6E400002-..."   // phone writes commands here
#define NUS_TX      "6E400003-..."   // badge could notify back (unused for now)
```

**`hexVal(c)`** — turns one hex character ('0'-'9', 'a'-'f', 'A'-'F') into its
number. Used to parse draw commands.

**`RxCallback::onWrite`** — runs the instant a BLE write lands:

```cpp
if (v[0]=='P' && v[1]==':') {          // draw command → handle RIGHT NOW
  // P:xyRRGGBB → parse x, y (one hex digit each) and the 6-digit color
  setPx(x, y, CRGB(...));              // paint straight into the frame buffer
  // P:C → FastLED.clear()
  drawDirty = true;                     // tell loop() "something changed, show it"
  return;
}
bleBuf = v; newBleMsg = true;          // everything else waits for loop()
```

Why two paths? Normal commands are rare (one button press), so a single
message buffer is fine. But drawing sends a burst of messages while you drag a
finger — if each had to wait for `loop()` to pick it up, some would overwrite
`bleBuf` before being read and pixels would drop. Painting directly in the
callback makes strokes lossless.

**`SrvCallback::onDisconnect`** — one job: restart advertising when the phone
disconnects, so the badge is immediately discoverable again.

**`setupBLE()`** — boots the radio: sets the name ("EyeD Badge"), creates the
NUS service with its RX (write) and TX (notify) characteristics, attaches the
callbacks, starts advertising.

**`handleBleMsg()`** — called from `loop()` when `newBleMsg` is set. Parses the
command language:

| Message      | Meaning |
|--------------|---------|
| `B:40`       | brightness 1-255 |
| `C:FF0044`   | set `fgColor` (hex RGB) |
| `M:5`        | switch to mode 5; `M:17` also clears the draw canvas |
| `P:xyRRGGBB` | (handled earlier in the callback) paint pixel x,y |
| `P:C`        | clear the draw canvas |
| anything else| treat as text → scroll it (mode 2) |

## 1.10 `setup()` and `loop()`

**`setup()`** — runs once: register the LED strip with FastLED (`WS2812B`,
`GRB` color order), set brightness, cap power draw at 5V/1.5A (battery
safety), start BLE, seed the random generator from hardware entropy, randomize
the rain columns.

**`loop()`** — the heartbeat:

```cpp
if (newBleMsg) handleBleMsg();   // apply any pending command first
switch (mode) { ... }            // then draw one frame of the current mode
```

- `MODE_NAME` → scroll `NAME_TEXT`; when a pass completes, switch to eyes for
  `EYES_DWELL_MS`, then back — that's the idle alternation.
- `MODE_EYES` → eye frame; only auto-returns to name if it *came from* name
  mode (`eyesUntil != 0`). Booting straight into eyes stays in eyes forever.
- `MODE_TEXT` → scroll whatever BLE text arrived (defaults to "HELLO").
- `MODE_CYCLE` → a sub-mode list and a 10-second timer; `runAnim(subs[sub])`
  plays the current one, timer advances to the next.
- `MODE_DRAW` → does *no* drawing of its own. The canvas lives in `leds[]`;
  when the BLE callback set `drawDirty`, push it out with one `show()`.
  This is why your drawing persists — nothing ever clears it until you leave.
- everything else → `runAnim(mode)`.

**`runAnim(m)`** — a lookup that calls the right `draw...Frame()` plus its
per-mode frame delay (plasma runs at 30 ms, Game of Life at a chill 140 ms...).
Shared by the normal path and cycle mode so the timings live in one place.

---

# PART 2 — The Web Page (`index.html`)

One self-contained file: styles, markup, and three JS sections (BLE, simulator,
draw). No frameworks, no build step.

## 2.1 Page structure (the HTML)

- **header** — title plus a status dot + "standby / linked" label
- **`<canvas id="cv">`** — the 8×16 LED preview (320×640 = 40 px per LED)
- **connect button** — the only thing active before you link
- **`#controls`** — everything else, blurred out (`.hidden`) until connected:
  - text input + send
  - mode buttons (generated from a JS list)
  - draw section: slider toggle, swatch palette, custom color, clear
  - brightness slider
  - color swatches
- **`#log`** — one-line status/debug readout at the bottom

## 2.2 The BLE section

```js
const NUS_SERVICE='6e400001-...', NUS_RX='6e400002-...';
let device=null, rxChar=null;    // the connected device and its RX characteristic
```

**`setUI(connected)`** — flips every visual between standby and linked states:
button text, glow dot, unblurring the controls. On disconnect it also turns
draw mode off and blanks the preview.

**`toggleConnect()`** — the whole connection dance:

```js
device = await navigator.bluetooth.requestDevice({
  filters: [{ namePrefix: 'EyeD' }],          // only badges in the picker
  optionalServices: [NUS_SERVICE]
});
const sv = await (await device.gatt.connect()).getPrimaryService(NUS_SERVICE);
rxChar  = await sv.getCharacteristic(NUS_RX); // grab the write pipe
```

Also registers a `gattserverdisconnected` listener so the UI resets if the
badge goes out of range. If the browser has no `navigator.bluetooth` (iPhone
Safari), it logs the "use Bluefy" hint instead.

**`sendCmd(cmd)`** — encodes the string to bytes and writes it to `rxChar` in
**20-byte chunks** — the safe payload size for a default BLE connection.

**`sendText()` / `uiBrightness()` / `pickMode()` / `pickColor()`** — thin
wrappers: read the input, send the matching command (`M:`, `B:`, `C:` or raw
text), update which button is highlighted, and mirror the change in the local
simulator so the preview matches the badge.

```js
const MODES=[['name',0,'scroll'],['eyes',1,'eyes'],...];
```
Each entry = button label, firmware mode number, simulator animation name.
The buttons are generated from this list — add a mode in firmware, add one line
here, done.

## 2.3 The LED simulator

The preview isn't a video — it's a **reimplementation of every firmware
animation in JavaScript**, drawing into the same kind of buffer:

```js
const W=8, H=16;
let buf = new Float32Array(W*H*3);   // the JS "frame buffer", RGB 0..1 floats
function setP(x,y,r,g,b) { ... }     // = firmware setPx
function fade(f) { ... }             // = fadeToBlackBy
function clearBuf() { ... }          // = FastLED.clear
```

- **`FDATA`** — the same 5×7 font as the firmware, as a JS object.
- **`S`** — an object of animation functions: `S.scroll`, `S.eyes`, `S.rain`,
  `S.fire`, `S.nyan`, ... each a line-for-line translation of its firmware
  twin (same constants, same wave math), so the preview and the badge stay in
  sync visually.
- **`S.breathe`** — the standby state: clears the buffer = all LEDs dark.
- **`simSet(mode, label)`** — switches the simulated mode and updates the
  little label under the canvas.

**The render loop** (`frame()` + `draw()`), driven by `requestAnimationFrame`:

- `IV` maps each mode to its frame interval (mirroring the firmware's per-mode
  `delay()`s); `frame()` steps the animation only when enough time has passed,
  but re-renders the canvas at full 60 fps.
- `draw()` paints every LED as a circle: a faint gray disc when off (so you see
  the grid), and when lit, a colored disc with `shadowBlur` glow — that's what
  makes it look like actual LEDs instead of flat squares.

## 2.4 The draw section

```js
let drawOn=false, painting=false, lastCell=-1, drawHex='39ff14';
S.draw = () => {};   // draw mode animates nothing — buf IS the artwork
```

- **`setDrawUI(on)`** — flips the slider visual (`.tog.on` CSS class), shows or
  hides the tools, switches the canvas cursor, and — important on phones —
  sets `touch-action:none` so painting doesn't scroll the page.
- **`toggleDraw()`** — the slider handler: on = send `M:17` (badge opens a
  blank canvas) and switch the sim to draw mode; off = back to the eye (`M:1`).
- **`setDrawColor(hex, el)`** — called by the swatches (same palette as the
  color section) or the native color input; stores the current paint color and
  moves the "active" highlight.
- **`cellFromEvt(e)`** — converts a mouse/touch position into a grid cell:
  take the canvas's on-screen rectangle, scale to 8×16, floor to integers.
- **`paint(e, start)`** — the brush:
  1. find the cell; skip if the drag is still in the same cell (`lastCell`)
  2. if it's a fresh tap on a pixel that already has the current color →
     paint **black** instead (that's tap-again-to-erase)
  3. write the color into `buf` (instant preview)
  4. if connected, `sendCmd('P:' + x + y + hex)` — one 10-character message,
     the firmware paints the same pixel
- **pointer events** — `pointerdown` starts a stroke (and captures the
  pointer so dragging off the canvas keeps working), `pointermove` paints
  while dragging, `pointerup` ends the stroke. Pointer events cover mouse,
  touch, and stylus with one API.
- **`clearDraw()`** — wipes the local buffer and sends `P:C`.

## 2.5 The command protocol (both sides, one table)

| Command      | Sent by page when...            | Firmware reaction |
|--------------|--------------------------------|-------------------|
| `M:0`–`M:17` | mode button / draw toggle      | switch mode (17 = blank draw canvas) |
| `B:5`–`B:150`| brightness slider release      | `FastLED.setBrightness` |
| `C:RRGGBB`   | color swatch tap               | set `fgColor` for all fgColor-based modes |
| `P:xyRRGGBB` | each painted cell in draw mode | set pixel (x,y) — handled in the BLE callback for speed |
| `P:C`        | clear button                   | clear the canvas |
| plain text   | send button / Enter            | scroll it (mode 2) |

That's the entire interface between the two files — six message shapes.
