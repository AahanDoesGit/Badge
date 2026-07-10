/*
 * EyeD BADGE FIRMWARE v1 — ESP32-C6 + 8x16 WS2812B matrix
 * Worn in portrait: logical canvas is 8 wide x 16 tall, text scrolls
 * bottom-to-top like credits. XY() maps portrait coords onto the
 * landscape-wired panel.
 * ------------------------------------------------------
 * Modes:
 *   0 = Name scroll (default, auto-alternates with eyes)
 *   1 = Idle eyes (blink + look around)
 *   2 = Custom BLE text scroll
 *   3 = Matrix rain   4 = Plasma   5 = Fire   6 = Sparkle
 *   7 = Auto-cycle (rotates all animations every 10s)
 *   8 = Shapes  9 = Game of Life  10 = Bounce  11 = Equalizer
 *   12 = Radar  13 = Comet        14 = Stars   15 = Pulse
 *
 * BLE (Nordic UART Service — use "Serial Bluetooth Terminal" app
 * or nRF Connect on phone, no custom app needed):
 *   Send any text        -> scrolls it (switches to mode 2)
 *   B:40                 -> brightness 0-255
 *   C:39FF14             -> hex color (neon green default)
 *   M:0 ... M:7          -> force mode
 *
 * Libraries: FastLED >= 3.7, ESP32 Arduino core >= 3.1 (C6 support)
 * Board: "ESP32C6 Dev Module"
 */

#include <FastLED.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ---------------- CONFIG ----------------
#define DATA_PIN      0
#define WIDTH         8         // logical width as worn (portrait)
#define HEIGHT        16        // logical height as worn
#define PANEL_W       16        // physical matrix wiring
#define PANEL_H       8
#define NUM_LEDS      (PANEL_W * PANEL_H)
#define SERPENTINE    false     // false if your PCB is progressive-wired
#define ROTATE_180    true      // flip display 180° (panel mounted upside-down)
#define NAME_TEXT     "Aahans"
#define SCROLL_MS     70        // lower = faster scroll
#define EYES_DWELL_MS 8000      // eyes duration between name scrolls

CRGB leds[NUM_LEDS];
CRGB fgColor = CRGB(0x39, 0xFF, 0x14);   // neon green
uint8_t brightness = 30;

// ---------------- STATE ----------------
enum Mode { MODE_NAME, MODE_EYES, MODE_TEXT, MODE_RAIN, MODE_PLASMA, MODE_FIRE, MODE_SPARKLE, MODE_CYCLE,
            MODE_SHAPES, MODE_LIFE, MODE_BOUNCE, MODE_EQ, MODE_RADAR, MODE_COMET, MODE_STARS, MODE_PULSE };
volatile Mode mode = MODE_EYES;   // boot into idle eyes (no text until sent)
String bleText = "";
volatile bool newBleMsg = false;
String bleBuf = "";

// ---------------- XY MAPPING ----------------
uint16_t XY(int8_t x, int8_t y) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return NUM_LEDS; // off-grid guard
#if ROTATE_180
  x = WIDTH - 1 - x;
  y = HEIGHT - 1 - y;
#endif
  int8_t px = HEIGHT - 1 - y;   // rotate portrait 90° onto landscape panel
  int8_t py = x;
  if (SERPENTINE && (py & 1))
    return py * PANEL_W + (PANEL_W - 1 - px);
  return py * PANEL_W + px;
}

void setPx(int8_t x, int8_t y, CRGB c) {
  uint16_t i = XY(x, y);
  if (i < NUM_LEDS) leds[i] = c;
}

// ---------------- 5x7 FONT (ASCII 32..90, lowercase mapped up) ----------------
const uint8_t font5x7[][5] PROGMEM = {
  {0x00,0x00,0x00,0x00,0x00}, // space
  {0x00,0x00,0x5F,0x00,0x00}, // !
  {0x00,0x07,0x00,0x07,0x00}, // "
  {0x14,0x7F,0x14,0x7F,0x14}, // #
  {0x24,0x2A,0x7F,0x2A,0x12}, // $
  {0x23,0x13,0x08,0x64,0x62}, // %
  {0x36,0x49,0x55,0x22,0x50}, // &
  {0x00,0x05,0x03,0x00,0x00}, // '
  {0x00,0x1C,0x22,0x41,0x00}, // (
  {0x00,0x41,0x22,0x1C,0x00}, // )
  {0x08,0x2A,0x1C,0x2A,0x08}, // *
  {0x08,0x08,0x3E,0x08,0x08}, // +
  {0x00,0x50,0x30,0x00,0x00}, // ,
  {0x08,0x08,0x08,0x08,0x08}, // -
  {0x00,0x60,0x60,0x00,0x00}, // .
  {0x20,0x10,0x08,0x04,0x02}, // /
  {0x3E,0x51,0x49,0x45,0x3E}, // 0
  {0x00,0x42,0x7F,0x40,0x00}, // 1
  {0x42,0x61,0x51,0x49,0x46}, // 2
  {0x21,0x41,0x45,0x4B,0x31}, // 3
  {0x18,0x14,0x12,0x7F,0x10}, // 4
  {0x27,0x45,0x45,0x45,0x39}, // 5
  {0x3C,0x4A,0x49,0x49,0x30}, // 6
  {0x01,0x71,0x09,0x05,0x03}, // 7
  {0x36,0x49,0x49,0x49,0x36}, // 8
  {0x06,0x49,0x49,0x29,0x1E}, // 9
  {0x00,0x36,0x36,0x00,0x00}, // :
  {0x00,0x56,0x36,0x00,0x00}, // ;
  {0x00,0x08,0x14,0x22,0x41}, // <
  {0x14,0x14,0x14,0x14,0x14}, // =
  {0x41,0x22,0x14,0x08,0x00}, // >
  {0x02,0x01,0x51,0x09,0x06}, // ?
  {0x32,0x49,0x79,0x41,0x3E}, // @
  {0x7E,0x11,0x11,0x11,0x7E}, // A
  {0x7F,0x49,0x49,0x49,0x36}, // B
  {0x3E,0x41,0x41,0x41,0x22}, // C
  {0x7F,0x41,0x41,0x22,0x1C}, // D
  {0x7F,0x49,0x49,0x49,0x41}, // E
  {0x7F,0x09,0x09,0x09,0x01}, // F
  {0x3E,0x41,0x49,0x49,0x7A}, // G
  {0x7F,0x08,0x08,0x08,0x7F}, // H
  {0x00,0x41,0x7F,0x41,0x00}, // I
  {0x20,0x40,0x41,0x3F,0x01}, // J
  {0x7F,0x08,0x14,0x22,0x41}, // K
  {0x7F,0x40,0x40,0x40,0x40}, // L
  {0x7F,0x02,0x0C,0x02,0x7F}, // M
  {0x7F,0x04,0x08,0x10,0x7F}, // N
  {0x3E,0x41,0x41,0x41,0x3E}, // O
  {0x7F,0x09,0x09,0x09,0x06}, // P
  {0x3E,0x41,0x51,0x21,0x5E}, // Q
  {0x7F,0x09,0x19,0x29,0x46}, // R
  {0x46,0x49,0x49,0x49,0x31}, // S
  {0x01,0x01,0x7F,0x01,0x01}, // T
  {0x3F,0x40,0x40,0x40,0x3F}, // U
  {0x1F,0x20,0x40,0x20,0x1F}, // V
  {0x7F,0x20,0x18,0x20,0x7F}, // W
  {0x63,0x14,0x08,0x14,0x63}, // X
  {0x07,0x08,0x70,0x08,0x07}, // Y
  {0x61,0x51,0x49,0x45,0x43}, // Z
};

uint8_t fontCol(char c, uint8_t col) {
  if (c >= 'a' && c <= 'z') c -= 32;          // to uppercase
  if (c < 32 || c > 90) c = '?';
  return pgm_read_byte(&font5x7[c - 32][col]);
}

// ---------------- SCROLLER ----------------
// vertical: text enters at the top and scrolls down,
// one upright letter per 8 rows, centered in the 8-px width
int scrollOffset;
int textPixelHeight(const String &s) { return s.length() * 8; } // 7px + 1 gap

// returns true when one full pass is complete
bool drawScrollFrame(const String &s) {
  FastLED.clear();
  int yStart = scrollOffset - textPixelHeight(s);  // enters top, moves down
  for (uint16_t ci = 0; ci < s.length(); ci++) {
    int cy = yStart + ci * 8;
    if (cy <= -8 || cy >= HEIGHT) continue;
    for (uint8_t col = 0; col < 5; col++) {
      uint8_t bits = fontCol(s[ci], col);
      for (uint8_t row = 0; row < 7; row++) {
        if (bits & (1 << row)) setPx(1 + col, cy + row, fgColor);
      }
    }
  }
  FastLED.show();
  scrollOffset++;
  if (scrollOffset > textPixelHeight(s) + HEIGHT) { scrollOffset = 0; return true; }
  return false;
}

// ---------------- EYE ----------------
// single "Big Brother" eye — almond outline, wandering red pupil, random blinks
int8_t pupilX = 0, pupilTargetX = 0;
uint8_t blinkPhase = 0;          // 0 = open, >0 counting through a blink
uint32_t nextBlinkAt = 0, nextLookAt = 0;

void drawEyesFrame() {
  uint32_t now = millis();

  if (blinkPhase == 0 && now > nextBlinkAt) blinkPhase = 1;
  if (now > nextLookAt) {
    pupilTargetX = (int8_t)random8(3) - 1;   // -1..1
    nextLookAt = now + 900 + random16(2200);
  }
  if (pupilX < pupilTargetX) pupilX++;
  else if (pupilX > pupilTargetX) pupilX--;

  uint8_t openness = 6;
  if (blinkPhase) {
    // 1..12 frame blink: close then open
    openness = (blinkPhase <= 6) ? 6 - blinkPhase : blinkPhase - 6;
    blinkPhase++;
    if (blinkPhase > 12) { blinkPhase = 0; nextBlinkAt = now + 1500 + random16(4000); }
  }

  FastLED.clear();
  const float cx = 3.5f, cy = 7.5f, a = 4.0f;      // centered in portrait
  float b = 0.6f + 3.2f * openness / 6.0f;   // lid height follows blink
  for (int8_t x = 0; x < WIDTH; x++)
    for (int8_t y = 0; y < HEIGHT; y++) {
      float dx = (x - cx) / a, dy = (y - cy) / b;
      float d = dx * dx + dy * dy;
      if (d <= 1.0f && d >= 0.45f) setPx(x, y, fgColor);  // almond outline
    }

  if (openness >= 3) {                       // iris + pupil visible
    int8_t ix = 3 + pupilX;
    CRGB iris = fgColor;
    iris.nscale8_video(80);
    for (int8_t x = ix - 1; x <= ix + 2; x++)
      for (int8_t y = 6; y <= 9; y++) {
        if ((x == ix - 1 || x == ix + 2) && (y == 6 || y == 9)) continue; // round the iris
        setPx(x, y, iris);
      }
    setPx(ix, 7, CRGB::Red);  setPx(ix + 1, 7, CRGB::Red);  // pupil
    setPx(ix, 8, CRGB::Red);  setPx(ix + 1, 8, CRGB::Red);
  }
  FastLED.show();
}

// ---------------- ANIMATIONS ----------------
// 3: Matrix rain — falling code drops in fgColor with bright heads
int8_t rainHead[WIDTH];

void drawRainFrame() {
  fadeToBlackBy(leds, NUM_LEDS, 70);            // trails fade out
  for (uint8_t x = 0; x < WIDTH; x++) {
    if (rainHead[x] < 0) {                      // waiting to respawn
      rainHead[x]++;
      if (rainHead[x] == 0 && random8() > 60) rainHead[x] = -1; // stagger
      continue;
    }
    setPx(x, rainHead[x], CRGB::White);         // bright head
    if (rainHead[x] > 0) setPx(x, rainHead[x] - 1, fgColor);
    rainHead[x]++;
    if (rainHead[x] >= HEIGHT + 2)
      rainHead[x] = -(int8_t)(1 + random8(14)); // random pause off-screen
  }
  FastLED.show();
}

// 4: Plasma — flowing sine color field
void drawPlasmaFrame() {
  uint32_t t = millis() / 6;
  for (uint8_t x = 0; x < WIDTH; x++)
    for (uint8_t y = 0; y < HEIGHT; y++) {
      uint8_t v = sin8(x * 22 + t) / 3
                + sin8(y * 40 - t * 2) / 3
                + sin8((x + y) * 14 + t / 2) / 3;
      leds[XY(x, y)] = CHSV(v + t / 5, 255, 255);
    }
  FastLED.show();
}

// 5: Fire — classic heat simulation, base at bottom row
uint8_t heat[WIDTH][HEIGHT];

void drawFireFrame() {
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++)
      heat[x][y] = qsub8(heat[x][y], random8(0, 20));          // cool
    for (uint8_t y = 0; y < HEIGHT - 1; y++)                    // rise
      heat[x][y] = (heat[x][y + 1] * 3 + heat[x][min(y + 2, HEIGHT - 1)]) / 4;
    if (random8() < 130)                                        // spark at base
      heat[x][HEIGHT - 1] = qadd8(heat[x][HEIGHT - 1], random8(120, 255));
    for (uint8_t y = 0; y < HEIGHT; y++)
      leds[XY(x, y)] = HeatColor(heat[x][y]);
  }
  FastLED.show();
}

// 6: Sparkle — random glitter in fgColor + white pops
void drawSparkleFrame() {
  fadeToBlackBy(leds, NUM_LEDS, 45);
  if (random8() < 170) setPx(random8(WIDTH), random8(HEIGHT), fgColor);
  if (random8() < 70)  setPx(random8(WIDTH), random8(HEIGHT), CRGB::White);
  FastLED.show();
}

// 8: Shapes — random geometric outlines popping in, fading out
uint32_t nextShapeAt = 0;

void drawShapesFrame() {
  fadeToBlackBy(leds, NUM_LEDS, 26);
  if (millis() > nextShapeAt) {
    nextShapeAt = millis() + 300 + random16(600);
    CRGB c = CHSV(random8(), 190, 255);
    int8_t cx = random8(WIDTH), cy = random8(HEIGHT);
    switch (random8(3)) {
      case 0: {                                  // rectangle outline
        int8_t w = 3 + random8(4), h = 3 + random8(2);
        for (int8_t x = 0; x < w; x++) { setPx(cx + x, cy, c); setPx(cx + x, cy + h - 1, c); }
        for (int8_t y = 0; y < h; y++) { setPx(cx, cy + y, c); setPx(cx + w - 1, cy + y, c); }
        break;
      }
      case 1:                                    // cross
        for (int8_t d = -2; d <= 2; d++) { setPx(cx + d, cy, c); setPx(cx, cy + d, c); }
        break;
      case 2:                                    // diamond
        for (int8_t d = 0; d <= 2; d++) {
          setPx(cx + d, cy + (2 - d), c); setPx(cx - d, cy + (2 - d), c);
          setPx(cx + d, cy - (2 - d), c); setPx(cx - d, cy - (2 - d), c);
        }
        break;
    }
  }
  FastLED.show();
}

// 9: Game of Life — evolves with wraparound, reseeds when dead/stagnant
uint8_t lifeGrid[WIDTH][HEIGHT];
uint16_t lifeGen = 0;

void seedLife() {
  for (uint8_t x = 0; x < WIDTH; x++)
    for (uint8_t y = 0; y < HEIGHT; y++)
      lifeGrid[x][y] = (random8() < 85) ? 1 : 0;
  lifeGen = 0;
}

void drawLifeFrame() {
  uint8_t nextG[WIDTH][HEIGHT];
  uint16_t alive = 0;
  for (uint8_t x = 0; x < WIDTH; x++)
    for (uint8_t y = 0; y < HEIGHT; y++) {
      uint8_t n = 0;
      for (int8_t dx = -1; dx <= 1; dx++)
        for (int8_t dy = -1; dy <= 1; dy++) {
          if (!dx && !dy) continue;
          n += lifeGrid[(x + dx + WIDTH) % WIDTH][(y + dy + HEIGHT) % HEIGHT];
        }
      nextG[x][y] = lifeGrid[x][y] ? (n == 2 || n == 3) : (n == 3);
      alive += nextG[x][y];
    }
  memcpy(lifeGrid, nextG, sizeof(lifeGrid));
  lifeGen++;
  if (alive < 3 || lifeGen > 120) seedLife();

  fadeToBlackBy(leds, NUM_LEDS, 90);             // dying cells leave trails
  for (uint8_t x = 0; x < WIDTH; x++)
    for (uint8_t y = 0; y < HEIGHT; y++)
      if (lifeGrid[x][y]) setPx(x, y, fgColor);
  FastLED.show();
}

// 10: Bounce — two balls with trails
float ballX[2] = {2, 5}, ballY[2] = {3, 12};
float ballVX[2] = {0.42, -0.31}, ballVY[2] = {0.27, -0.35};

void drawBounceFrame() {
  fadeToBlackBy(leds, NUM_LEDS, 42);
  for (uint8_t i = 0; i < 2; i++) {
    ballX[i] += ballVX[i]; ballY[i] += ballVY[i];
    if (ballX[i] <= 0 || ballX[i] >= WIDTH - 1)  { ballVX[i] = -ballVX[i]; ballX[i] = constrain(ballX[i], 0.0f, (float)(WIDTH - 1)); }
    if (ballY[i] <= 0 || ballY[i] >= HEIGHT - 1) { ballVY[i] = -ballVY[i]; ballY[i] = constrain(ballY[i], 0.0f, (float)(HEIGHT - 1)); }
    setPx((int8_t)ballX[i], (int8_t)ballY[i], i ? fgColor : CRGB(CRGB::White));
  }
  FastLED.show();
}

// 11: Equalizer — noise-driven bars, green base / amber mid / red peak
uint8_t eqLevel[WIDTH];

void drawEqFrame() {
  FastLED.clear();
  for (uint8_t x = 0; x < WIDTH; x++) {
    uint8_t target = inoise8(x * 35, millis() / 3) / 18;   // 0..14
    if (target > HEIGHT) target = HEIGHT;
    if (eqLevel[x] < target) eqLevel[x]++;
    else if (eqLevel[x] > target) eqLevel[x]--;
    for (uint8_t h = 0; h < eqLevel[x]; h++) {
      CRGB c = (h >= 12) ? CRGB(CRGB::Red) : (h >= 8) ? CRGB(255, 160, 0) : fgColor;
      setPx(x, HEIGHT - 1 - h, c);
    }
  }
  FastLED.show();
}

// 12: Radar — expanding ripple rings from random points
struct Ripple { int8_t x, y; uint8_t r; bool on; };
Ripple ripples[3];

void drawRadarFrame() {
  fadeToBlackBy(leds, NUM_LEDS, 55);
  if (random8() < 18) {
    for (uint8_t i = 0; i < 3; i++)
      if (!ripples[i].on) {
        ripples[i].x = random8(WIDTH); ripples[i].y = random8(HEIGHT);
        ripples[i].r = 0; ripples[i].on = true;
        break;
      }
  }
  for (uint8_t i = 0; i < 3; i++) {
    if (!ripples[i].on) continue;
    int16_t rOut = ripples[i].r * ripples[i].r;
    int16_t rIn  = (ripples[i].r - 1) * (ripples[i].r - 1);
    for (int8_t x = 0; x < WIDTH; x++)
      for (int8_t y = 0; y < HEIGHT; y++) {
        int16_t d2 = (x - ripples[i].x) * (x - ripples[i].x) + (y - ripples[i].y) * (y - ripples[i].y);
        if (d2 <= rOut && d2 > rIn) setPx(x, y, fgColor);
      }
    ripples[i].r++;
    if (ripples[i].r > 12) ripples[i].on = false;
  }
  FastLED.show();
}

// 13: Comet — smooth full-height scanner sweep, hue drifts
void drawCometFrame() {
  fadeToBlackBy(leds, NUM_LEDS, 38);
  uint8_t pos = beatsin8(24, 0, WIDTH - 1);
  CRGB c = CHSV(millis() / 40, 255, 255);
  for (uint8_t y = 0; y < HEIGHT; y++) setPx(pos, y, c);
  FastLED.show();
}

// 14: Stars — twinkling starfield with respawn
struct Star { uint8_t x, y, b; int8_t db; };
Star stars[14];

void drawStarsFrame() {
  FastLED.clear();
  for (uint8_t i = 0; i < 14; i++) {
    int16_t nb = stars[i].b + stars[i].db;
    if (nb >= 240) { nb = 240; stars[i].db = -(int8_t)(2 + random8(4)); }
    if (nb <= 0) {
      stars[i].x = random8(WIDTH); stars[i].y = random8(HEIGHT);
      stars[i].db = (int8_t)(2 + random8(5)); nb = 1;
    }
    stars[i].b = (uint8_t)nb;
    setPx(stars[i].x, stars[i].y, CRGB(stars[i].b, stars[i].b, qadd8(stars[i].b, 12)));
  }
  FastLED.show();
}

// 15: Pulse — whole panel breathing in fgColor
void drawPulseFrame() {
  uint8_t v = beatsin8(16, 8, 160);
  CRGB c = fgColor;
  c.nscale8_video(v);
  fill_solid(leds, NUM_LEDS, c);
  FastLED.show();
}

// ---------------- BLE (Nordic UART Service) ----------------
#define NUS_SERVICE "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

class RxCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *ch) override {
    String v = ch->getValue();
    if (v.length()) { bleBuf = String(v.c_str()); newBleMsg = true; }
  }
};

class SrvCallback : public BLEServerCallbacks {
  void onDisconnect(BLEServer *s) override { s->getAdvertising()->start(); }
};

void setupBLE() {
  BLEDevice::init("EyeD Badge");
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new SrvCallback());
  BLEService *svc = server->createService(NUS_SERVICE);
  BLECharacteristic *rx = svc->createCharacteristic(
      NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCallback());
  BLECharacteristic *tx = svc->createCharacteristic(
      NUS_TX, BLECharacteristic::PROPERTY_NOTIFY);
  tx->addDescriptor(new BLE2902());
  svc->start();
  server->getAdvertising()->addServiceUUID(NUS_SERVICE);
  server->getAdvertising()->start();
}

void handleBleMsg() {
  newBleMsg = false;
  String msg = bleBuf;
  msg.trim();
  if (!msg.length()) return;

  if (msg.startsWith("B:")) {
    brightness = constrain(msg.substring(2).toInt(), 1, 255);
    FastLED.setBrightness(brightness);
  } else if (msg.startsWith("C:") && msg.length() >= 8) {
    long v = strtol(msg.substring(2, 8).c_str(), NULL, 16);
    fgColor = CRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
  } else if (msg.startsWith("M:")) {
    int m = msg.substring(2).toInt();
    if (m >= 0 && m <= 15) { mode = (Mode)m; scrollOffset = 0; }
  } else {
    bleText = msg;
    mode = MODE_TEXT;
    scrollOffset = 0;
  }
}

// ---------------- MAIN ----------------
uint32_t eyesUntil = 0;

void setup() {
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(brightness);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 1500);  // safety cap for battery
  setupBLE();
  random16_set_seed(esp_random());
  for (uint8_t x = 0; x < WIDTH; x++) rainHead[x] = -(int8_t)(1 + random8(14));
}

void loop() {
  if (newBleMsg) handleBleMsg();

  switch (mode) {
    case MODE_NAME:
      if (drawScrollFrame(NAME_TEXT)) {         // full pass done -> eyes break
        mode = MODE_EYES;
        eyesUntil = millis() + EYES_DWELL_MS;
      }
      delay(SCROLL_MS);
      break;

    case MODE_EYES:
      drawEyesFrame();
      if (eyesUntil && millis() > eyesUntil) {  // auto-return only if timed
        mode = MODE_NAME;
        eyesUntil = 0;
        scrollOffset = 0;
      }
      delay(40);
      break;

    case MODE_TEXT:
      drawScrollFrame(bleText.length() ? bleText : String("HELLO"));
      delay(SCROLL_MS);
      break;

    case MODE_CYCLE: {
      static uint8_t sub = 0;
      static uint32_t nextSwitch = 0;
      static const Mode subs[] = {
        MODE_RAIN, MODE_PLASMA, MODE_FIRE, MODE_SPARKLE, MODE_SHAPES, MODE_LIFE,
        MODE_BOUNCE, MODE_EQ, MODE_RADAR, MODE_COMET, MODE_STARS, MODE_PULSE, MODE_EYES
      };
      if (millis() > nextSwitch) {
        sub = (sub + 1) % (sizeof(subs) / sizeof(subs[0]));
        nextSwitch = millis() + 10000;   // 10s per animation
        FastLED.clear();
      }
      runAnim(subs[sub]);
      break;
    }

    default:          // all plain animation modes
      runAnim(mode);
      break;
  }
}

// dispatch one animation frame + its frame delay
void runAnim(Mode m) {
  switch (m) {
    case MODE_EYES:    drawEyesFrame();    delay(40);  break;
    case MODE_RAIN:    drawRainFrame();    delay(65);  break;
    case MODE_PLASMA:  drawPlasmaFrame();  delay(30);  break;
    case MODE_FIRE:    drawFireFrame();    delay(45);  break;
    case MODE_SPARKLE: drawSparkleFrame(); delay(40);  break;
    case MODE_SHAPES:  drawShapesFrame();  delay(40);  break;
    case MODE_LIFE:    drawLifeFrame();    delay(140); break;
    case MODE_BOUNCE:  drawBounceFrame();  delay(35);  break;
    case MODE_EQ:      drawEqFrame();      delay(40);  break;
    case MODE_RADAR:   drawRadarFrame();   delay(70);  break;
    case MODE_COMET:   drawCometFrame();   delay(25);  break;
    case MODE_STARS:   drawStarsFrame();   delay(40);  break;
    case MODE_PULSE:   drawPulseFrame();   delay(30);  break;
    default: break;
  }
}
