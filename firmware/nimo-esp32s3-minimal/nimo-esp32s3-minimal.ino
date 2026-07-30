#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <ESP_I2S.h>
#include <math.h>
#include <time.h>
#include "private_config.h"

#ifndef NIMO_API_KEY
#define NIMO_API_KEY ""
#endif

WiFiClientSecure nimoSecureClient;
bool nimoSecureClientReady = false;

bool nimoHttpBegin(HTTPClient& http, const String& url) {
  bool ok;
  if (url.startsWith("https://")) {
    if (!nimoSecureClientReady) {
      nimoSecureClient.setInsecure();
      nimoSecureClientReady = true;
    }
    ok = http.begin(nimoSecureClient, url);
  } else {
    ok = http.begin(url);
  }
  if (ok && strlen(NIMO_API_KEY) > 0) {
    http.addHeader("X-Nimo-Key", NIMO_API_KEY);
  }
  return ok;
}

unsigned long lastHeartbeatAt = 0;
unsigned long lastPollAt = 0;
unsigned long lastRegisterAt = 0;
const unsigned long HEARTBEAT_INTERVAL_MS = 30000;
const unsigned long POLL_INTERVAL_MS = 10000;
const unsigned long REGISTER_INTERVAL_MS = 60000;
const unsigned long SPEECH_POLL_INTERVAL_MS = 3500;
const int MAX_RECORD_SECONDS = 15;
const int MIN_RECORD_SAMPLES = 16000 / 2;
bool deviceRegistered = false;
const int STATUS_RGB_PIN = -1;
const int LCD_MOSI = 47;
const int LCD_SCLK = 21;
const int LCD_CS = 2;
const int LCD_DC = 3;
const int LCD_RST = -1;
const int XL9555_SDA = 38;
const int XL9555_SCL = 48;
const uint8_t XL9555_ADDR = 0x20;
const uint16_t XL9555_LCD_CS = 0x2000;     // P15 on the Hiwonder board.
const uint16_t XL9555_BACKLIGHT = 0x4000; // P16 on the Hiwonder board.
const uint16_t XL9555_AUDIO_MUTE = 0x8000; // P17, HIGH enables the ES8311/NS4150B audio path.
const uint8_t XL9555_OUTPUT_PORT1_REG = 3;
const uint8_t XL9555_INPUT_PORT0_REG = 0;
const uint8_t XL9555_INPUT_PORT1_REG = 1;
const uint8_t XL9555_CONFIG_PORT0_REG = 6;
const uint8_t XL9555_CONFIG_PORT1_REG = 7;
const uint16_t XL9555_KEY1 = 0x0010; // P04, active low.
const int BOOT_KEY_PIN = 0;          // On-board BOOT/KEY button, active low.
const int PIN_I2C_SDA = 4;
const int PIN_I2C_SCL = 5;
const uint8_t ES8311_I2C_ADDR = 0x18;
const int PIN_I2S_MCLK = 45;
const int PIN_I2S_BCLK = 39;
const int PIN_I2S_LRCK = 41;
const int PIN_I2S_DOUT = 42;
const int PIN_I2S_DIN = 40;
unsigned long lastLedAt = 0;
unsigned long lastKey1At = 0;
bool statusLedOn = false;
bool displayReady = false;
bool audioReady = false;
bool key1WasPressed = false;
bool voiceFlowActive = false;
bool playbackInterrupted = false;
unsigned long voiceFlowUntil = 0;
unsigned long lastSpeechPollAt = 0;
unsigned long fastSpeechPollUntil = 0;
String currentReminderText = "";
String lastSpeechId = "";
String lastRenderKey = "";
enum StatusLedMode {
  STATUS_WIFI_CONNECTING,
  STATUS_NIMO_OK,
  STATUS_NIMO_ERROR
};
StatusLedMode statusLedMode = STATUS_WIFI_CONNECTING;
Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, LCD_CS, LCD_DC, LCD_RST);
U8G2_FOR_ADAFRUIT_GFX u8f;
TwoWire codecWire = TwoWire(1);
I2SClass audioI2s;

void xl9555WriteReg(uint8_t reg, uint8_t data) {
  Wire.beginTransmission(XL9555_ADDR);
  Wire.write(reg);
  Wire.write(data);
  Wire.endTransmission();
}

uint8_t xl9555ReadReg(uint8_t reg) {
  Wire.beginTransmission(XL9555_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  Wire.requestFrom(XL9555_ADDR, (uint8_t)1);
  if (!Wire.available()) return 0xFF;
  return Wire.read();
}

void xl9555ConfigOutput(uint16_t pin) {
  bool port1 = pin > 0x00FF;
  uint8_t bit = pin >> (port1 ? 8 : 0);
  uint8_t reg = port1 ? XL9555_CONFIG_PORT1_REG : XL9555_CONFIG_PORT0_REG;
  uint8_t value = xl9555ReadReg(reg);
  if (value == 0xFF) value = 0xFF;
  xl9555WriteReg(reg, value & ~bit);
}

void xl9555ConfigInput(uint16_t pin) {
  bool port1 = pin > 0x00FF;
  uint8_t bit = pin >> (port1 ? 8 : 0);
  uint8_t reg = port1 ? XL9555_CONFIG_PORT1_REG : XL9555_CONFIG_PORT0_REG;
  uint8_t value = xl9555ReadReg(reg);
  if (value == 0xFF) value = 0xFF;
  xl9555WriteReg(reg, value | bit);
}

void xl9555SetPin(uint16_t pin, bool high) {
  uint8_t bit = pin >> 8;
  uint8_t value = xl9555ReadReg(XL9555_OUTPUT_PORT1_REG);
  if (value == 0xFF) value = 0x00;
  value = high ? (value | bit) : (value & ~bit);
  xl9555WriteReg(XL9555_OUTPUT_PORT1_REG, value);
}

bool xl9555GetPin(uint16_t pin) {
  bool port1 = pin > 0x00FF;
  uint8_t bit = pin >> (port1 ? 8 : 0);
  uint8_t reg = port1 ? XL9555_INPUT_PORT1_REG : XL9555_INPUT_PORT0_REG;
  uint8_t value = xl9555ReadReg(reg);
  if (value == 0xFF) return true;
  return (value & bit) != 0;
}

bool isKey1Pressed() {
  return !xl9555GetPin(XL9555_KEY1) || digitalRead(BOOT_KEY_PIN) == LOW;
}

bool es8311WriteReg(uint8_t reg, uint8_t value) {
  codecWire.beginTransmission(ES8311_I2C_ADDR);
  codecWire.write(reg);
  codecWire.write(value);
  int err = codecWire.endTransmission();
  if (err != 0) {
    Serial.printf("[ES8311] write reg 0x%02X = 0x%02X failed (err=%d)\n", reg, value, err);
    return false;
  }
  return true;
}

uint8_t es8311ReadReg(uint8_t reg) {
  codecWire.beginTransmission(ES8311_I2C_ADDR);
  codecWire.write(reg);
  if (codecWire.endTransmission(false) != 0) return 0xFF;
  if (codecWire.requestFrom((uint8_t)ES8311_I2C_ADDR, (uint8_t)1) != 1) return 0xFF;
  if (codecWire.available()) return codecWire.read();
  return 0xFF;
}

bool es8311Init() {
  uint8_t before = es8311ReadReg(0x00);
  Serial.printf("[ES8311] reg00 before init: 0x%02X\n", before);
  if (!es8311WriteReg(0x00, 0x1F)) return false;
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

  uint8_t reg00 = es8311ReadReg(0x00);
  uint8_t reg31 = es8311ReadReg(0x31);
  uint8_t reg32 = es8311ReadReg(0x32);
  uint8_t reg44 = es8311ReadReg(0x44);
  Serial.printf("[ES8311] reg00=0x%02X reg31=0x%02X reg32=0x%02X reg44=0x%02X\n", reg00, reg31, reg32, reg44);
  return reg00 != 0xFF && reg31 != 0xFF && reg32 != 0xFF && reg44 != 0xFF;
}

bool setupAudio() {
  xl9555ConfigOutput(XL9555_AUDIO_MUTE);
  xl9555SetPin(XL9555_AUDIO_MUTE, true);
  codecWire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  delay(50);

  audioI2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT, PIN_I2S_DIN, PIN_I2S_MCLK);
  if (!audioI2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("[AUDIO] I2S begin failed.");
    return false;
  }
  static int16_t silence[256 * 2] = {0};
  for (int i = 0; i < 8; i++) audioI2s.write((uint8_t*)silence, sizeof(silence));

  bool ok = es8311Init();
  Serial.printf("[AUDIO] setup %s\n", ok ? "OK" : "FAIL");
  return ok;
}

void playTone(uint16_t durationMs = 180, float freq = 880.0f) {
  if (!audioReady) return;
  const int sampleRate = 16000;
  const int framesPerBuf = 128;
  int16_t buf[framesPerBuf * 2];
  float phase = 0.0f;
  const float dPhase = 2.0f * (float)M_PI * freq / (float)sampleRate;
  unsigned long start = millis();
  while (millis() - start < durationMs) {
    for (int i = 0; i < framesPerBuf; i++) {
      int16_t sample = (int16_t)(sinf(phase) * 24000.0f);
      buf[i * 2] = sample;
      buf[i * 2 + 1] = sample;
      phase += dPhase;
      if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
    }
    audioI2s.write((uint8_t*)buf, sizeof(buf));
  }
  memset(buf, 0, sizeof(buf));
  for (int i = 0; i < 4; i++) audioI2s.write((uint8_t*)buf, sizeof(buf));
}

bool playPcmAudioPath(const String& path) {
  if (!audioReady || WiFi.status() != WL_CONNECTED) return false;
  playbackInterrupted = false;
  HTTPClient http;
  String url = String(NIMO_BASE_URL) + path;
  http.useHTTP10(true);
  nimoHttpBegin(http, url);
  http.setTimeout(30000);
  int status = http.GET();
  Serial.printf("GET %s -> %d\n", url.c_str(), status);
  if (!isHttpOk(status)) {
    String payload = http.getString();
    if (payload.length()) Serial.println(payload);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t monoBytes[512];
  int16_t stereo[256 * 2];
  uint8_t carry = 0;
  bool hasCarry = false;
  unsigned long lastDataAt = millis();
  unsigned long lastKeyCheckAt = 0;
  int totalMonoBytes = 0;

  int16_t silence[64 * 2] = {0};
  while (http.connected() || stream->available()) {
    if (millis() - lastKeyCheckAt > 120) {
      lastKeyCheckAt = millis();
      if (isKey1Pressed()) {
        playbackInterrupted = true;
        Serial.println("[AUDIO] playback interrupted by key");
        break;
      }
    }
    int available = stream->available();
    if (available <= 0) {
      if (millis() - lastDataAt > 8000) break;
      audioI2s.write((uint8_t*)silence, sizeof(silence));
      continue;
    }
    int got = stream->readBytes(monoBytes, min(available, (int)sizeof(monoBytes)));
    if (got <= 0) continue;
    lastDataAt = millis();
    totalMonoBytes += got;

    int outFrames = 0;
    int offset = 0;
    if (hasCarry && got > 0) {
      int16_t sample = (int16_t)((uint16_t)carry | ((uint16_t)monoBytes[0] << 8));
      stereo[outFrames * 2] = sample;
      stereo[outFrames * 2 + 1] = sample;
      outFrames++;
      offset = 1;
      hasCarry = false;
    }
    for (int i = offset; i + 1 < got && outFrames < 256; i += 2) {
      int16_t sample = (int16_t)((uint16_t)monoBytes[i] | ((uint16_t)monoBytes[i + 1] << 8));
      stereo[outFrames * 2] = sample;
      stereo[outFrames * 2 + 1] = sample;
      outFrames++;
    }
    if (((got - offset) & 1) != 0) {
      carry = monoBytes[got - 1];
      hasCarry = true;
    }
    if (outFrames > 0) {
      audioI2s.write((uint8_t*)stereo, outFrames * 2 * sizeof(int16_t));
    }
  }

  memset(stereo, 0, sizeof(stereo));
  for (int i = 0; i < 5; i++) audioI2s.write((uint8_t*)stereo, sizeof(stereo));
  http.end();
  Serial.printf("[AUDIO] streamed pcm bytes=%d interrupted=%d\n", totalMonoBytes, playbackInterrupted ? 1 : 0);
  return totalMonoBytes > 0;
}

bool playSpeechAudio(const String& speechId, const String& text) {
  if (!speechId.length()) return false;
  renderAssistantScreen("speaking", "", text, "", "");
  String path = String("/device/speech/audio?id=") + urlEncode(speechId) + "&format=pcm";
  bool ok = playPcmAudioPath(path);
  if (!ok) playTone(360, 660.0f);
  return ok;
}

bool restartAudioI2s() {
  audioI2s.end();
  delay(20);
  audioI2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT, PIN_I2S_DIN, PIN_I2S_MCLK);
  bool ok = audioI2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH);
  Serial.printf("[AUDIO] i2s restart %s\n", ok ? "OK" : "FAIL");
  return ok;
}

// Push-to-talk capture: keeps recording while KEY1 is held, up to maxSamples.
int recordPcmMonoWhileHeld(int16_t* out, int maxSamples) {
  if (!audioReady || !out || maxSamples <= 0) return 0;
  restartAudioI2s();
  const int framesPerBuf = 128;
  int16_t stereo[framesPerBuf * 2];
  int writtenSamples = 0;
  int maxLevel = 0;
  unsigned long start = millis();
  unsigned long lastKeyCheckAt = 0;
  bool held = true;
  int releasedReads = 0;
  static int16_t txSilence[framesPerBuf * 2] = {0};
  i2s_chan_handle_t rxChan = audioI2s.rxChan();
  if (rxChan == NULL) {
    Serial.println("[MIC] rx channel NULL");
    return 0;
  }
  i2s_channel_disable(rxChan);
  i2s_channel_enable(rxChan);
  i2s_chan_handle_t txChan = audioI2s.txChan();
  Serial.printf("[MIC] ptt start t=%lu max=%d\n", millis(), maxSamples);
  while (held && writtenSamples < maxSamples) {
    // Key polling goes over I2C which can glitch; require several
    // consecutive released reads before treating the key as released.
    if (millis() - lastKeyCheckAt > 40) {
      lastKeyCheckAt = millis();
      if (isKey1Pressed()) {
        releasedReads = 0;
      } else {
        releasedReads++;
        Serial.printf("[MIC] key released read #%d at %lu\n", releasedReads, millis() - start);
        if (releasedReads >= 4) held = false;
      }
    }
    if (txChan != NULL) {
      size_t txDone = 0;
      i2s_channel_write(txChan, (char*)txSilence, sizeof(txSilence), &txDone, 20);
    }
    size_t got = 0;
    esp_err_t rerr = i2s_channel_read(rxChan, (char*)stereo, sizeof(stereo), &got, 200);
    if (rerr != ESP_OK) continue;
    int frames = got / (sizeof(int16_t) * 2);
    for (int i = 0; i < frames && writtenSamples < maxSamples; i++) {
      int sample = ((int)stereo[i * 2] + (int)stereo[i * 2 + 1]) / 2;
      if (sample > 32767) sample = 32767;
      if (sample < -32768) sample = -32768;
      out[writtenSamples++] = (int16_t)sample;
      int v = abs(sample);
      if (v > maxLevel) maxLevel = v;
    }
  }
  Serial.printf("[MIC] ptt recorded samples=%d peak=%d elapsed=%lu\n", writtenSamples, maxLevel, millis() - start);
  return writtenSamples;
}

int postPcm(const int16_t* pcm, int samples, int peak, String* responsePayload = nullptr) {
  if (!pcm || samples <= 0) return -1;
  HTTPClient http;
  String url = String(NIMO_BASE_URL) + "/device/voice/pcm?deviceId=" + DEVICE_ID + "&sampleRate=16000&peak=" + String(peak);
  nimoHttpBegin(http, url);
  http.setTimeout(25000);
  http.addHeader("Content-Type", "application/octet-stream");
  int status = http.POST((uint8_t*)pcm, samples * sizeof(int16_t));
  String payload = http.getString();
  Serial.printf("POST %s bytes=%d -> %d\n", url.c_str(), samples * 2, status);
  if (payload.length()) Serial.println(payload);
  if (responsePayload) *responsePayload = payload;
  http.end();
  return status;
}

void setupHiwonderExpander() {
  pinMode(BOOT_KEY_PIN, INPUT_PULLUP);
  Wire.begin(XL9555_SDA, XL9555_SCL, 400000);
  delay(50);
  xl9555ConfigOutput(XL9555_LCD_CS);
  xl9555ConfigOutput(XL9555_BACKLIGHT);
  xl9555ConfigInput(XL9555_KEY1);
  xl9555SetPin(XL9555_LCD_CS, true);
  xl9555SetPin(XL9555_BACKLIGHT, true);
}

void setBacklight(bool on) {
  xl9555SetPin(XL9555_BACKLIGHT, on);
}

String extractJsonString(const String& payload, const char* key) {
  String marker = String("\"") + key + "\":\"";
  int start = payload.indexOf(marker);
  if (start < 0) return "";
  start += marker.length();
  String value;
  bool escaped = false;
  for (int i = start; i < payload.length(); i++) {
    char ch = payload[i];
    if (escaped) {
      if (ch == 'n') value += ' ';
      else if (ch == 'u') {
        value += '?';
        i += 4;
      }
      else value += ch;
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') break;
    value += ch;
  }
  return value;
}

String extractJsonNumber(const String& payload, const char* key) {
  String marker = String("\"") + key + "\":";
  int start = payload.indexOf(marker);
  if (start < 0) return "";
  start += marker.length();
  String value;
  for (int i = start; i < payload.length(); i++) {
    char ch = payload[i];
    if ((ch >= '0' && ch <= '9') || ch == '-') value += ch;
    else if (value.length()) break;
  }
  return value;
}

String urlEncode(const String& value) {
  String encoded;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < value.length(); i++) {
    unsigned char ch = value[i];
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      encoded += (char)ch;
    } else {
      encoded += '%';
      encoded += hex[ch >> 4];
      encoded += hex[ch & 0x0F];
    }
  }
  return encoded;
}

String dueTimeLabel(const String& dueAt) {
  if (dueAt.length() >= 16) return dueAt.substring(11, 16);
  return dueAt;
}

String normalizedState(String state) {
  state.trim();
  state.toLowerCase();
  if (state == "online") return "idle";
  if (state == "wifi ok") return "idle";
  if (state == "wifi...") return "thinking";
  if (state == "nimo error") return "error";
  if (!state.length()) return "idle";
  return state;
}

// ---------- UTF-8 / Chinese rendering ----------

void drawUtf8(int16_t x, int16_t y, const String& text, uint16_t color, const uint8_t* font) {
  u8f.setFont(font);
  u8f.setForegroundColor(color);
  u8f.setCursor(x, y);
  u8f.print(text);
}

void drawUtf8Centered(int16_t y, const String& text, uint16_t color, const uint8_t* font) {
  u8f.setFont(font);
  int16_t w = u8f.getUTF8Width(text.c_str());
  int16_t x = (320 - w) / 2;
  if (x < 0) x = 0;
  u8f.setForegroundColor(color);
  u8f.setCursor(x, y);
  u8f.print(text);
}

int utf8CharLen(unsigned char ch) {
  if (ch < 0x80) return 1;
  if ((ch & 0xE0) == 0xC0) return 2;
  if ((ch & 0xF0) == 0xE0) return 3;
  if ((ch & 0xF8) == 0xF0) return 4;
  return 1;
}

// Wraps UTF-8 text into lines no wider than maxWidth; returns the y after the last line.
int drawWrappedUtf8(int16_t x, int16_t y, int16_t maxWidth, int16_t lineHeight, int maxLines,
                    const String& text, uint16_t color, const uint8_t* font) {
  u8f.setFont(font);
  u8f.setForegroundColor(color);
  String line;
  int lines = 0;
  size_t i = 0;
  while (i < text.length() && lines < maxLines) {
    int len = utf8CharLen((unsigned char)text[i]);
    String ch = text.substring(i, i + len);
    i += len;
    String candidate = line + ch;
    if (u8f.getUTF8Width(candidate.c_str()) > maxWidth && line.length()) {
      u8f.setCursor(x, y);
      u8f.print(line);
      y += lineHeight;
      lines++;
      line = ch;
    } else {
      line = candidate;
    }
  }
  if (line.length() && lines < maxLines) {
    if (i < text.length()) line += "...";
    u8f.setCursor(x, y);
    u8f.print(line);
    y += lineHeight;
  }
  return y;
}

bool shouldRender(const String& key) {
  if (key == lastRenderKey) return false;
  lastRenderKey = key;
  return true;
}

void invalidateDisplayCache() {
  lastRenderKey = "";
}

String clockText() {
  struct tm tmNow;
  if (!getLocalTime(&tmNow, 50)) return "";
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", tmNow.tm_hour, tmNow.tm_min);
  return String(buf);
}

String dateText() {
  struct tm tmNow;
  if (!getLocalTime(&tmNow, 50)) return "";
  static const char* weekdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
  char buf[32];
  snprintf(buf, sizeof(buf), "%d月%d日 %s", tmNow.tm_mon + 1, tmNow.tm_mday, weekdays[tmNow.tm_wday]);
  return String(buf);
}

void drawStatusBar(bool online) {
  tft.fillCircle(300, 14, 5, online ? ST77XX_GREEN : ST77XX_RED);
  drawUtf8(230, 20, online ? "在线" : "离线", online ? ST77XX_GREEN : ST77XX_RED, u8g2_font_wqy14_t_gb2312);
}

void renderBootScreen(const String& step1, const String& step2, const String& step3) {
  if (!displayReady) return;
  tft.fillScreen(ST77XX_BLACK);
  drawUtf8Centered(50, "尼莫 Nimo", ST77XX_CYAN, u8g2_font_wqy16_t_gb2312);
  tft.drawFastHLine(40, 66, 240, ST77XX_BLUE);
  if (step1.length()) drawUtf8(60, 110, step1, ST77XX_WHITE, u8g2_font_wqy16_t_gb2312);
  if (step2.length()) drawUtf8(60, 145, step2, ST77XX_WHITE, u8g2_font_wqy16_t_gb2312);
  if (step3.length()) drawUtf8(60, 180, step3, ST77XX_WHITE, u8g2_font_wqy16_t_gb2312);
  invalidateDisplayCache();
}

void renderAssistantScreen(const String& state, const String& line1, const String& line2, const String& line3, const String& line4) {
  if (!displayReady) return;
  String mode = normalizedState(state);
  bool online = deviceRegistered && WiFi.status() == WL_CONNECTED;
  String key = String("assistant|") + mode + "|" + line1 + "|" + line2 + "|" + line3 + "|" + line4 + "|" + (online ? "1" : "0") + "|" + clockText();
  if (!shouldRender(key)) return;
  setBacklight(true);
  tft.fillScreen(ST77XX_BLACK);

  if (mode == "listening") {
    tft.fillCircle(120, 40, 4, ST77XX_CYAN);
    tft.fillCircle(200, 40, 4, ST77XX_CYAN);
    drawUtf8Centered(75, "正在聆听", ST77XX_CYAN, u8g2_font_wqy16_t_gb2312);
    tft.fillRoundRect(88, 110, 20, 46, 4, ST77XX_BLUE);
    tft.fillRoundRect(128, 85, 20, 71, 4, ST77XX_CYAN);
    tft.fillRoundRect(168, 101, 20, 55, 4, ST77XX_BLUE);
    tft.fillRoundRect(208, 77, 20, 79, 4, ST77XX_CYAN);
    drawUtf8Centered(200, "松开按键立即发送 · 最长15秒", ST77XX_GREEN, u8g2_font_wqy14_t_gb2312);
    return;
  }

  if (mode == "thinking") {
    drawStatusBar(online);
    // Simple pondering face.
    tft.drawCircle(160, 70, 26, ST77XX_YELLOW);
    tft.fillCircle(150, 62, 3, ST77XX_YELLOW);
    tft.fillCircle(170, 62, 3, ST77XX_YELLOW);
    tft.drawFastHLine(150, 82, 20, ST77XX_YELLOW);
    drawUtf8Centered(130, "思考中…", ST77XX_YELLOW, u8g2_font_wqy16_t_gb2312);
    if (line2.length()) drawWrappedUtf8(20, 165, 280, 24, 2, line2, ST77XX_WHITE, u8g2_font_wqy14_t_gb2312);
    return;
  }

  if (mode == "speaking") {
    drawStatusBar(online);
    drawUtf8(10, 24, "尼莫", ST77XX_CYAN, u8g2_font_wqy16_t_gb2312);
    tft.drawFastHLine(10, 36, 300, ST77XX_BLUE);
    String reply = line2.length() ? line2 : line1;
    drawWrappedUtf8(12, 70, 296, 26, 5, reply, ST77XX_WHITE, u8g2_font_wqy16_t_gb2312);
    drawUtf8Centered(226, "按住按键可打断并说话", ST77XX_GREEN, u8g2_font_wqy14_t_gb2312);
    return;
  }

  if (mode == "reminder") {
    drawUtf8Centered(40, "提醒", ST77XX_YELLOW, u8g2_font_wqy16_t_gb2312);
    tft.drawFastHLine(40, 56, 240, ST77XX_BLUE);
    String content = line1.length() ? line1 : "时间到了";
    drawWrappedUtf8(20, 100, 280, 28, 3, content, ST77XX_WHITE, u8g2_font_wqy16_t_gb2312);
    if (line2.length()) drawUtf8Centered(190, line2, ST77XX_GREEN, u8g2_font_wqy14_t_gb2312);
    drawUtf8Centered(222, "按住按键说话", ST77XX_CYAN, u8g2_font_wqy14_t_gb2312);
    return;
  }

  if (mode == "memory") {
    drawStatusBar(online);
    drawUtf8(10, 24, "已记住", ST77XX_MAGENTA, u8g2_font_wqy16_t_gb2312);
    tft.drawFastHLine(10, 36, 300, ST77XX_BLUE);
    drawWrappedUtf8(12, 70, 296, 26, 4, line2.length() ? line2 : line1, ST77XX_WHITE, u8g2_font_wqy16_t_gb2312);
    return;
  }

  if (mode == "error") {
    drawStatusBar(false);
    drawUtf8Centered(70, "未连接", ST77XX_YELLOW, u8g2_font_wqy16_t_gb2312);
    String reason = line1.length() ? line1 : "无法连接 Nimo 服务";
    drawWrappedUtf8(20, 110, 280, 24, 2, reason, ST77XX_WHITE, u8g2_font_wqy14_t_gb2312);
    drawUtf8Centered(190, "请确认电脑已打开 Nimo 软件", ST77XX_CYAN, u8g2_font_wqy14_t_gb2312);
    drawUtf8Centered(215, "且与设备在同一 Wi-Fi", ST77XX_CYAN, u8g2_font_wqy14_t_gb2312);
    return;
  }

  // Idle: big clock + date + next reminder + hint.
  drawStatusBar(online);
  // Small smiley next to the clock area.
  tft.drawCircle(36, 60, 16, ST77XX_CYAN);
  tft.fillCircle(30, 55, 2, ST77XX_CYAN);
  tft.fillCircle(42, 55, 2, ST77XX_CYAN);
  tft.drawFastHLine(30, 66, 12, ST77XX_CYAN);
  String hhmm = clockText();
  if (hhmm.length()) {
    u8f.setFont(u8g2_font_logisoso42_tn);
    int16_t w = u8f.getUTF8Width(hhmm.c_str());
    u8f.setForegroundColor(ST77XX_WHITE);
    u8f.setCursor((320 - w) / 2, 90);
    u8f.print(hhmm);
    drawUtf8Centered(120, dateText(), ST77XX_CYAN, u8g2_font_wqy14_t_gb2312);
  } else {
    drawUtf8Centered(80, "尼莫在这里", ST77XX_WHITE, u8g2_font_wqy16_t_gb2312);
  }
  tft.drawFastHLine(20, 140, 280, ST77XX_BLUE);
  String reminder = line3.length() ? line3 : "暂无提醒";
  drawUtf8(20, 170, "下一条：", ST77XX_GREEN, u8g2_font_wqy14_t_gb2312);
  drawWrappedUtf8(90, 170, 210, 22, 2, reminder, ST77XX_WHITE, u8g2_font_wqy14_t_gb2312);
  drawUtf8Centered(226, "按住按键说话，松开发送", ST77XX_MAGENTA, u8g2_font_wqy14_t_gb2312);
}

void renderDisplay(const String& state, const String& detail, const String& reminder) {
  String mode = normalizedState(state);
  renderAssistantScreen(mode, detail, "", reminder, "");
}

void renderScreenLines(const String& state, const String& line1, const String& line2, const String& line3, const String& line4) {
  renderAssistantScreen(state, line1, line2, line3, line4);
}

void setupDisplay() {
  setupHiwonderExpander();
  setBacklight(true);
  SPI.begin(LCD_SCLK, -1, LCD_MOSI, LCD_CS);
  tft.setSPISpeed(40000000);
  tft.init(240, 320);
  tft.setRotation(1);
  u8f.begin(tft);
  u8f.setFontMode(1);
  u8f.setBackgroundColor(ST77XX_BLACK);
  displayReady = true;
  invalidateDisplayCache();
  renderBootScreen("屏幕 √", "启动中…", "");
}

String connectionDetail() {
  if (WiFi.status() == WL_CONNECTED) return String("IP ") + WiFi.localIP().toString();
  return String("WiFi ") + WIFI_SSID;
}

void renderCurrentStatus(const String& state) {
  renderDisplay(state, connectionDetail(), currentReminderText);
}

void writeStatusLed(uint8_t red, uint8_t green, uint8_t blue) {
  if (STATUS_RGB_PIN < 0) return;
  neopixelWrite(STATUS_RGB_PIN, red, green, blue);
}

void setupStatusLed() {
  if (STATUS_RGB_PIN < 0) return;
  pinMode(STATUS_RGB_PIN, OUTPUT);
  writeStatusLed(0, 0, 0);
}

void updateStatusLed() {
  unsigned long now = millis();
  unsigned long interval = 350;
  if (statusLedMode == STATUS_NIMO_OK) interval = 1200;
  if (now - lastLedAt < interval) return;
  lastLedAt = now;
  statusLedOn = !statusLedOn;
  if (!statusLedOn) {
    writeStatusLed(0, 0, 0);
    return;
  }
  if (statusLedMode == STATUS_WIFI_CONNECTING) {
    writeStatusLed(0, 0, 36);
  } else if (statusLedMode == STATUS_NIMO_OK) {
    writeStatusLed(0, 36, 0);
  } else {
    writeStatusLed(36, 0, 0);
  }
}

bool isHttpOk(int status) {
  return status >= 200 && status < 300;
}

int postJson(const String& path, const String& body, String* responsePayload = nullptr) {
  HTTPClient http;
  String url = String(NIMO_BASE_URL) + path;
  nimoHttpBegin(http, url);
  http.addHeader("Content-Type", "application/json");
  int status = http.POST(body);
  String payload = http.getString();
  Serial.printf("POST %s -> %d\n", url.c_str(), status);
  if (payload.length()) Serial.println(payload);
  if (responsePayload) *responsePayload = payload;
  http.end();
  return status;
}

String getJson(const String& path) {
  HTTPClient http;
  String url = String(NIMO_BASE_URL) + path;
  nimoHttpBegin(http, url);
  int status = http.GET();
  String payload = http.getString();
  Serial.printf("GET %s -> %d\n", url.c_str(), status);
  if (payload.length()) Serial.println(payload);
  http.end();
  return payload;
}

void startVoiceInteraction() {
  if (WiFi.status() != WL_CONNECTED) {
    renderAssistantScreen("error", "Wi-Fi 已断开", "", "", "");
    voiceFlowActive = true;
    voiceFlowUntil = millis() + 4000;
    return;
  }

  voiceFlowActive = true;
  voiceFlowUntil = millis() + 30000;
  renderAssistantScreen("listening", "", "", "", "");
  playTone(120, 880.0f);

  const int maxSamples = 16000 * MAX_RECORD_SECONDS;
  int16_t* pcm = (int16_t*)ps_malloc(maxSamples * sizeof(int16_t));
  if (!pcm) pcm = (int16_t*)malloc(16000 * 3 * sizeof(int16_t));
  if (!pcm) {
    renderAssistantScreen("error", "内存不足，无法录音", "", "", "");
    voiceFlowUntil = millis() + 5000;
    return;
  }

  int samples = recordPcmMonoWhileHeld(pcm, maxSamples);
  if (samples < MIN_RECORD_SAMPLES) {
    free(pcm);
    renderAssistantScreen("thinking", "", "说话时间太短，请按住按键再说", "", "");
    voiceFlowUntil = millis() + 4000;
    return;
  }
  int micPeak = 0;
  for (int i = 0; i < samples; i++) {
    int v = abs((int)pcm[i]);
    if (v > micPeak) micPeak = v;
  }
  bool audioDetected = micPeak > 1200;
  if (!audioDetected) {
    free(pcm);
    renderAssistantScreen("thinking", "", "没有听清，请靠近一点再说", "", "");
    voiceFlowUntil = millis() + 5000;
    return;
  }

  renderAssistantScreen("thinking", "", "正在识别…", "", "");
  String payload;
  int status = postPcm(pcm, samples, micPeak, &payload);
  free(pcm);
  if (!isHttpOk(status) || payload.indexOf("\"ok\":true") < 0) {
    String err = extractJsonString(payload, "error");
    renderAssistantScreen("error", err.length() ? err : "语音请求失败", "", "", "");
    voiceFlowUntil = millis() + 5000;
    return;
  }

  String state = extractJsonString(payload, "state");
  String transcript = extractJsonString(payload, "transcript");
  String speechId = extractJsonString(payload, "speechId");
  if (!state.length()) state = "thinking";
  renderAssistantScreen(state, "", transcript, "", "");
  if (speechId.length()) {
    playSpeechAudio(speechId, "");
    ackSpeech(speechId);
  }
  fastSpeechPollUntil = millis() + 20000;
  voiceFlowUntil = millis() + 8000;
}

void ackSpeech(const String& speechId) {
  if (!speechId.length()) return;
  String body = String("{\"deviceId\":\"") + DEVICE_ID + "\",\"speechId\":\"" + speechId + "\"}";
  postJson("/device/speech/ack", body);
}

bool pollSpeechQueue() {
  if (WiFi.status() != WL_CONNECTED) return false;
  String payload = getJson(String("/device/speech/next?deviceId=") + DEVICE_ID);
  if (payload.indexOf("\"ok\":true") < 0 || payload.indexOf("\"speech\":null") >= 0) return false;
  String speechId = extractJsonString(payload, "id");
  String text = extractJsonString(payload, "text");
  if (!speechId.length() || speechId == lastSpeechId || !text.length()) return false;
  lastSpeechId = speechId;
  playSpeechAudio(speechId, text);
  ackSpeech(speechId);
  voiceFlowActive = true;
  voiceFlowUntil = millis() + 9000;
  if (playbackInterrupted) {
    startVoiceInteraction();
  }
  return true;
}

void handleButtons() {
  bool pressed = isKey1Pressed();
  unsigned long now = millis();
  if (pressed && !key1WasPressed && now - lastKey1At > 500) {
    lastKey1At = now;
    startVoiceInteraction();
    pressed = isKey1Pressed();
  }
  key1WasPressed = pressed;
}

void connectWifi() {
  statusLedMode = STATUS_WIFI_CONNECTING;
  renderBootScreen("屏幕 √  音频 " + String(audioReady ? "√" : "×"), String("连接 Wi-Fi：") + WIFI_SSID, "");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting WiFi: %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    updateStatusLed();
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
  renderBootScreen("屏幕 √  音频 " + String(audioReady ? "√" : "×"), "Wi-Fi √  " + WiFi.localIP().toString(), "连接 Nimo 服务…");
}

bool registerDevice() {
  String body = String("{")
    + "\"id\":\"" + DEVICE_ID + "\","
    + "\"name\":\"" + DEVICE_NAME + "\","
    + "\"type\":\"esp32-s3\","
    + "\"capabilities\":[\"screen\",\"speaker\",\"touch\",\"button\",\"voice-trigger\"]"
    + "}";
  String payload;
  int status = postJson("/device/register", body, &payload);
  deviceRegistered = isHttpOk(status) && payload.indexOf("\"ok\":true") >= 0;
  if (deviceRegistered) {
    statusLedMode = STATUS_NIMO_OK;
    Serial.println("Device registered.");
    invalidateDisplayCache();
    renderCurrentStatus("Online");
  } else {
    statusLedMode = STATUS_NIMO_ERROR;
    Serial.println("Device register failed, will retry.");
    invalidateDisplayCache();
    renderCurrentStatus("Nimo Error");
  }
  lastRegisterAt = millis();
  return deviceRegistered;
}

void sendHeartbeat() {
  String body = String("{\"deviceId\":\"") + DEVICE_ID + "\"}";
  int status = postJson("/device/heartbeat", body);
  if (!isHttpOk(status)) {
    deviceRegistered = false;
    statusLedMode = STATUS_NIMO_ERROR;
    renderCurrentStatus("Nimo Error");
  } else if (deviceRegistered) {
    statusLedMode = STATUS_NIMO_OK;
  }
}

void loadConfig() {
  getJson(String("/device/config?deviceId=") + DEVICE_ID);
}

void pollScreenStatus() {
  if (voiceFlowActive && millis() < voiceFlowUntil) return;
  voiceFlowActive = false;
  String payload = getJson(String("/device/screen-status?deviceId=") + DEVICE_ID);
  if (payload.indexOf("\"ok\":true") < 0) {
    statusLedMode = STATUS_NIMO_ERROR;
    renderCurrentStatus("Nimo Error");
    return;
  }
  String state = extractJsonString(payload, "state");
  String line1 = extractJsonString(payload, "line1");
  String line2 = extractJsonString(payload, "line2");
  String line3 = extractJsonString(payload, "line3");
  String line4 = extractJsonString(payload, "line4");
  if (!state.length()) state = "Online";
  if (!line3.length()) line3 = "";
  currentReminderText = line3;
  statusLedMode = STATUS_NIMO_OK;
  deviceRegistered = true;
  renderScreenLines(state, line1, line2, line3, line4);
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("Nimo ESP32-S3 minimal firmware starting...");
  Serial.println("Init status led...");
  setupStatusLed();
  Serial.println("Init display...");
  setupDisplay();
  Serial.println("Display ready.");
  Serial.println("Init audio...");
  audioReady = setupAudio();
  if (audioReady) playTone(120, 1040.0f);
  connectWifi();
  deviceRegistered = registerDevice();
  loadConfig();
  sendHeartbeat();
  pollScreenStatus();
}

void loop() {
  updateStatusLed();
  handleButtons();
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
    registerDevice();
  }

  unsigned long now = millis();
  if (!deviceRegistered || now - lastRegisterAt >= REGISTER_INTERVAL_MS) {
    registerDevice();
  }
  if (now - lastHeartbeatAt >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeatAt = now;
    sendHeartbeat();
  }
  unsigned long speechInterval = (now < fastSpeechPollUntil) ? 400 : SPEECH_POLL_INTERVAL_MS;
  if (now - lastSpeechPollAt >= speechInterval) {
    lastSpeechPollAt = now;
    if (pollSpeechQueue()) fastSpeechPollUntil = 0;
  }
  if (now - lastPollAt >= POLL_INTERVAL_MS) {
    lastPollAt = now;
    pollScreenStatus();
  }
  delay(60);
}
