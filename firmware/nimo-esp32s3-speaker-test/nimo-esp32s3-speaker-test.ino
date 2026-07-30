// Nimo ESP32-S3 Speaker Test
// Goal: prove the onboard ES8311 + NS4150B + speaker chain can make sound.
// Board: Hiwonder/幻尔 ESP32-S3 Large Model Board (ESP32-S3-WROOM-1)
// Pins (from board schematic):
//   I2C: SDA=GPIO4, SCL=GPIO5  (ES8311 control bus)
//   I2S: MCLK=GPIO45, BCLK=GPIO39, LRCK=GPIO41, DOUT(ESP->Codec)=GPIO42, DIN(Codec->ESP)=GPIO40
// Speaker PA: NS4150B (CTRL/MUTE net not yet mapped to a GPIO; if speaker stays silent,
//   we will probe additional candidates in the next iteration.)
//
// What this sketch does:
//   1) I2C scan and print all responsive addresses
//   2) ES8311 minimal init (slave mode, MCLK from MCLK pin, 16kHz mono 16-bit)
//   3) Output continuous 440Hz sine wave on I2S
//   4) Toggle a few candidate PA enable GPIOs HIGH in case the board needs an unmute
//
// Requires Arduino-ESP32 core >= 3.0 (which ships ESP_I2S.h).

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ESP_I2S.h>
#include <math.h>

// ── Audio pins ──────────────────────────────────────────────────────────────
static const int PIN_I2C_SDA = 4;
static const int PIN_I2C_SCL = 5;
static const int XL9555_SDA = 38;
static const int XL9555_SCL = 48;
static const uint8_t XL9555_ADDR = 0x20;
static const uint16_t XL9555_AUDIO_MUTE = 0x8000; // P17 in vendor sample, set HIGH to enable speaker/audio path.
static const uint8_t XL9555_OUTPUT_PORT1_REG = 3;
static const uint8_t XL9555_CONFIG_PORT1_REG = 7;
static const int PIN_I2S_MCLK = 45;
static const int PIN_I2S_BCLK = 39;
static const int PIN_I2S_LRCK = 41;
static const int PIN_I2S_DOUT = 42; // ESP32 -> ES8311 DSDIN
static const int PIN_I2S_DIN  = 40; // ES8311 ASDOUT -> ESP32

static const int LCD_MOSI = 47;
static const int LCD_SCLK = 21;
static const int LCD_CS = 2;
static const int LCD_DC = 3;
static const int LCD_RST = -1;

// ── ES8311 I2C address (CE pin tied low on this board => 7-bit 0x18) ──────
static const uint8_t ES8311_I2C_ADDR = 0x18;

// ── PA enable candidates (try a few common pins; safe to drive HIGH even if NC) ──
// Skip pins already used by LCD/Audio/PSRAM/Strapping to avoid conflicts.
static const int PA_EN_CANDIDATES[] = {};
static const size_t PA_EN_COUNT = sizeof(PA_EN_CANDIDATES) / sizeof(PA_EN_CANDIDATES[0]);

TwoWire expanderWire = TwoWire(1);
I2SClass i2s;
Adafruit_ST7789 tft = Adafruit_ST7789(LCD_CS, LCD_DC, LCD_MOSI, LCD_SCLK, LCD_RST);

static bool displayReady = false;
static int i2cFoundCount = 0;
static bool es8311Found = false;
static bool es8311Ready = false;
static bool i2sReady = false;
static bool tonePlayed = false;

static void xl9555WriteReg(uint8_t reg, uint8_t data) {
  expanderWire.beginTransmission(XL9555_ADDR);
  expanderWire.write(reg);
  expanderWire.write(data);
  expanderWire.endTransmission();
}

static uint8_t xl9555ReadReg(uint8_t reg) {
  expanderWire.beginTransmission(XL9555_ADDR);
  expanderWire.write(reg);
  if (expanderWire.endTransmission(false) != 0) return 0xFF;
  expanderWire.requestFrom(XL9555_ADDR, (uint8_t)1);
  if (!expanderWire.available()) return 0xFF;
  return expanderWire.read();
}

static void xl9555ConfigOutput(uint16_t pin) {
  uint8_t bit = pin >> 8;
  uint8_t value = xl9555ReadReg(XL9555_CONFIG_PORT1_REG);
  if (value == 0xFF) value = 0xFF;
  xl9555WriteReg(XL9555_CONFIG_PORT1_REG, value & ~bit);
}

static void xl9555SetPin(uint16_t pin, bool high) {
  uint8_t bit = pin >> 8;
  uint8_t value = xl9555ReadReg(XL9555_OUTPUT_PORT1_REG);
  if (value == 0xFF) value = 0x00;
  value = high ? (value | bit) : (value & ~bit);
  xl9555WriteReg(XL9555_OUTPUT_PORT1_REG, value);
}

static void enableAudioMutePin() {
  expanderWire.begin(XL9555_SDA, XL9555_SCL, 400000);
  delay(50);
  xl9555ConfigOutput(XL9555_AUDIO_MUTE);
  xl9555SetPin(XL9555_AUDIO_MUTE, true);
  Serial.println("[XL9555] audio MUTE/enable P17 -> HIGH");
}

static void drawStatus(const char* line1, const char* line2 = "", const char* line3 = "", const char* line4 = "") {
  Serial.printf("[STATUS] %s | %s | %s | %s\n", line1, line2, line3, line4);
  if (!displayReady) return;
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 12);
  tft.print("Nimo Audio");
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, 46);
  tft.print(line1);
  tft.setCursor(10, 66);
  tft.print(line2);
  tft.setCursor(10, 86);
  tft.print(line3);
  tft.setCursor(10, 106);
  tft.print(line4);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(10, 206);
  tft.print("Short tone diagnostic");
}

static void setupDisplay() {
  tft.init(240, 320);
  tft.setRotation(1);
  displayReady = true;
  drawStatus("Booting speaker test");
}

static bool es8311WriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(ES8311_I2C_ADDR);
  Wire.write(reg);
  Wire.write(value);
  int err = Wire.endTransmission();
  if (err != 0) {
    Serial.printf("[ES8311] write reg 0x%02X = 0x%02X failed (err=%d)\n", reg, value, err);
    return false;
  }
  return true;
}

static uint8_t es8311ReadReg(uint8_t reg) {
  Wire.beginTransmission(ES8311_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  if (Wire.requestFrom((uint8_t)ES8311_I2C_ADDR, (uint8_t)1) != 1) return 0xFF;
  if (Wire.available()) return Wire.read();
  return 0xFF;
}

static void scanI2C() {
  Serial.println("[I2C] scanning bus...");
  int found = 0;
  es8311Found = false;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C]   found device at 0x%02X\n", addr);
      found++;
      if (addr == ES8311_I2C_ADDR) es8311Found = true;
    }
  }
  i2cFoundCount = found;
  Serial.printf("[I2C] scan done, %d devices\n", found);
}

// ES8311 minimal init for 16kHz / 16-bit / mono / I2S slave / MCLK from pin
// Register meaning condensed from the ESP-ADF es8311 driver and ES8311 datasheet.
// MCLK divider target: 16000 * 256 = 4.096MHz from ESP32 I2S MCLK.
static bool es8311Init() {
  uint8_t before = es8311ReadReg(0x00);
  Serial.printf("[ES8311] reg 0x00 before init: 0x%02X\n", before);
  if (!es8311WriteReg(0x00, 0x1F)) {
    Serial.println("[ES8311] no ACK on 0x18 — codec not detected.");
    return false;
  }
  delay(20);

  es8311WriteReg(0x00, 0x00);
  es8311WriteReg(0x00, 0x80);
  es8311WriteReg(0x01, 0x3F);
  es8311WriteReg(0x02, 0x00);
  es8311WriteReg(0x03, 0x10);
  es8311WriteReg(0x04, 0x10);
  es8311WriteReg(0x05, 0x00);
  es8311WriteReg(0x06, 0x03);
  es8311WriteReg(0x07, 0x00);
  es8311WriteReg(0x08, 0xFF);
  es8311WriteReg(0x09, 0x0C);
  es8311WriteReg(0x0A, 0x0C);
  es8311WriteReg(0x0B, 0x00);
  es8311WriteReg(0x0C, 0x00);
  es8311WriteReg(0x0D, 0x01);
  es8311WriteReg(0x0E, 0x02);
  es8311WriteReg(0x10, 0x1F);
  es8311WriteReg(0x11, 0x7F);
  es8311WriteReg(0x12, 0x00);
  es8311WriteReg(0x13, 0x10);
  es8311WriteReg(0x14, 0x1A);
  es8311WriteReg(0x15, 0x40);
  es8311WriteReg(0x16, 0x24);
  es8311WriteReg(0x17, 0xBF);
  es8311WriteReg(0x1B, 0x0A);
  es8311WriteReg(0x1C, 0x6A);
  es8311WriteReg(0x31, 0x00);
  es8311WriteReg(0x32, 0xB2);
  es8311WriteReg(0x37, 0x08);
  es8311WriteReg(0x44, 0x08);
  es8311WriteReg(0x45, 0x00);

  Serial.println("[ES8311] init done.");
  uint8_t reg00 = es8311ReadReg(0x00);
  uint8_t reg32 = es8311ReadReg(0x32);
  uint8_t reg31 = es8311ReadReg(0x31);
  uint8_t reg44 = es8311ReadReg(0x44);
  Serial.printf("[ES8311] reg00=0x%02X reg31=0x%02X reg32=0x%02X reg44=0x%02X\n", reg00, reg31, reg32, reg44);
  es8311Ready = es8311Found && reg00 != 0xFF && reg31 != 0xFF && reg44 != 0xFF;
  return true;
}

static void enableAllPACandidates() {
  Serial.println("[PA] driving candidate enable pins HIGH (safe probe)...");
  for (size_t i = 0; i < PA_EN_COUNT; i++) {
    int pin = PA_EN_CANDIDATES[i];
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    Serial.printf("[PA]   GPIO%d -> HIGH\n", pin);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("=== Nimo Speaker Test (ES8311 + NS4150B) ===");
  setupDisplay();

  enableAudioMutePin();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  delay(50);
  drawStatus("Scanning I2C...");
  scanI2C();
  char line1[64];
  snprintf(line1, sizeof(line1), "I2C devices: %d, ES8311: %s", i2cFoundCount, es8311Found ? "YES" : "NO");
  drawStatus(line1, "Init ES8311...");

  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT, PIN_I2S_DIN, PIN_I2S_MCLK);
  if (!i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("[I2S] begin failed!");
    i2sReady = false;
  } else {
    Serial.println("[I2S] begin OK before ES8311 init, MCLK/BCLK/LRCK are running");
    i2sReady = true;
    static int16_t silence[256 * 2] = {0};
    for (int i = 0; i < 8; i++) i2s.write((uint8_t*)silence, sizeof(silence));
  }

  if (!es8311Init()) {
    Serial.println("[ES8311] init failed — continuing so I2S logs still appear.");
  }

  enableAllPACandidates();

  if (i2sReady) Serial.println("[I2S] playing 5s 880Hz sine @16kHz stereo 16-bit");

  char line2[64];
  char line3[64];
  snprintf(line2, sizeof(line2), "ES8311 init: %s", es8311Ready ? "OK" : "FAIL");
  snprintf(line3, sizeof(line3), "I2S: %s, tone: 5 seconds", i2sReady ? "OK" : "FAIL");
  drawStatus(line1, line2, line3, "Listen for one short beep");
}

void loop() {
  if (tonePlayed || !i2sReady) {
    delay(1000);
    return;
  }
  const int sampleRate = 16000;
  const float freq = 880.0f;
  const int framesPerBuf = 256;
  static int16_t buf[framesPerBuf * 2];
  static float phase = 0.0f;
  const float dPhase = 2.0f * (float)M_PI * freq / (float)sampleRate;

  unsigned long start = millis();
  while (millis() - start < 5000) {
    for (int i = 0; i < framesPerBuf; i++) {
      int16_t sample = (int16_t)(sinf(phase) * 28000.0f);
      buf[i * 2] = sample;
      buf[i * 2 + 1] = sample;
      phase += dPhase;
      if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
    }
    size_t written = i2s.write((uint8_t*)buf, sizeof(buf));
    if (written != sizeof(buf)) {
      Serial.printf("[I2S] short write: %u/%u\n", (unsigned)written, (unsigned)sizeof(buf));
      delay(5);
    }
  }

  memset(buf, 0, sizeof(buf));
  for (int i = 0; i < 20; i++) {
    i2s.write((uint8_t*)buf, sizeof(buf));
  }
  tonePlayed = true;
  drawStatus("Tone attempt done", es8311Ready ? "ES8311: OK" : "ES8311: FAIL", i2sReady ? "I2S: OK" : "I2S: FAIL", "No continuous sound");
}
