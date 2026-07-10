/*
 * ID BADGE FIRMWARE v1 — ESP32-C6 + 8x16 WS2812B matrix
 * ------------------------------------------------------
 * Modes:
 *   0 = Name scroll (default, auto-alternates with eyes)
 *   1 = Idle eyes (blink + look around)
 *   2 = Custom BLE text scroll
 *   3 = Matrix rain   4 = Plasma   5 = Fire   6 = Sparkle
 *   7 = Auto-cycle (rotates 3-6 + eyes every 10s)
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
#define DATA_PIN      4
#define WIDTH         16
#define HEIGHT        8
#define NUM_LEDS      (WIDTH * HEIGHT)
#define SERPENTINE    true      // false if your PCB is progressive-wired
#define NAME_TEXT     "Aahans"
#define SCROLL_MS     70        // lower = faster scroll
#define EYES_DWELL_MS 8000      // eyes duration between name scrolls

CRGB leds[NUM_LEDS];
CRGB fgColor = CRGB(0x39, 0xFF, 0x14);   // neon green
uint8_t brightness = 30;

// ---------------- STATE ----------------
enum Mode { MODE_NAME, MODE_EYES, MODE_TEXT, MODE_RAIN, MODE_PLASMA, MODE_FIRE, MODE_SPARKLE, MODE_CYCLE };
volatile Mode mode = MODE_NAME;
String bleText = "";
volatile bool newBleMsg = false;
String bleBuf = "";

// ---------------- XY MAPPING ----------------
uint16_t XY(int8_t x, int8_t y) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return NUM_LEDS; // off-grid guard
  if (SERPENTINE && (y & 1))
    return y * WIDTH + (WIDTH - 1 - x);
  return y * WIDTH + x;
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
int scrollOffset;
int textPixelWidth(const String &s) { return s.length() * 6; } // 5px + 1 gap

// returns true when one full pass is complete
bool drawScrollFrame(const String &s) {
  FastLED.clear();
  int xStart = WIDTH - scrollOffset;
  for (uint16_t ci = 0; ci < s.length(); ci++) {
    int cx = xStart + ci * 6;
    if (cx <= -6 || cx >= WIDTH) continue;
    for (uint8_t col = 0; col < 5; col++) {
      uint8_t bits = fontCol(s[ci], col);
      for (uint8_t row = 0; row < 7; row++) {
        if (bits & (1 << row)) setPx(cx + col, row, fgColor); // rows 0..6, row 7 blank
      }
    }
  }
  FastLED.show();
  scrollOffset++;
  if (scrollOffset > textPixelWidth(s) + WIDTH) { scrollOffset = 0; return true; }
  return false;
}

// ---------------- EYES ----------------
// two 5x6 eyes at x=2 and x=9, pupil wanders, random blinks
int8_t pupilX = 1, pupilTargetX = 1;
uint8_t blinkPhase = 0;          // 0 = open, >0 counting through a blink
uint32_t nextBlinkAt = 0, nextLookAt = 0;

void drawEye(int8_t ox, uint8_t openness) {
  // openness: 6 = fully open ... 0 = closed
  uint8_t top = (6 - openness) / 2 + 1;
  uint8_t bot = 7 - (6 - openness + 1) / 2;
  for (int8_t x = 0; x < 5; x++)
    for (int8_t y = top; y < bot; y++) {
      bool border = (x == 0 || x == 4 || y == top || y == bot - 1);
      if (border) setPx(ox + x, y, fgColor);
    }
  if (openness >= 4) { // pupil visible
    setPx(ox + 1 + pupilX, 3, fgColor);
    setPx(ox + 1 + pupilX, 4, fgColor);
  }
}

void drawEyesFrame() {
  uint32_t now = millis();

  if (blinkPhase == 0 && now > nextBlinkAt) blinkPhase = 1;
  if (now > nextLookAt) {
    pupilTargetX = random8(3);   // 0,1,2
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
  drawEye(2, openness);
  drawEye(9, openness);
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
  BLEDevice::init("BADGE-AYUSHMAAN");
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
    if (m >= 0 && m <= 7) { mode = (Mode)m; scrollOffset = 0; }
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

    case MODE_RAIN:    drawRainFrame();    delay(65); break;
    case MODE_PLASMA:  drawPlasmaFrame();  delay(30); break;
    case MODE_FIRE:    drawFireFrame();    delay(45); break;
    case MODE_SPARKLE: drawSparkleFrame(); delay(40); break;

    case MODE_CYCLE: {
      static uint8_t sub = 0;
      static uint32_t nextSwitch = 0;
      if (millis() > nextSwitch) {
        sub = (sub + 1) % 5;
        nextSwitch = millis() + 10000;   // 10s per animation
        FastLED.clear();
      }
      switch (sub) {
        case 0: drawRainFrame();    delay(65); break;
        case 1: drawPlasmaFrame();  delay(30); break;
        case 2: drawFireFrame();    delay(45); break;
        case 3: drawSparkleFrame(); delay(40); break;
        case 4: drawEyesFrame();    delay(40); break;
      }
      break;
    }
  }
}
