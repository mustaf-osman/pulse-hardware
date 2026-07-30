#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

struct Candidate {
  const char* name;
  int8_t cs;
  int8_t dc;
  int8_t mosi;
  int8_t sclk;
  int8_t rst;
  int8_t bl;
  uint16_t w;
  uint16_t h;
  uint8_t rotation;
};

Candidate candidates[] = {
  {"C1 HMI soft D6D7D8D9", D8, D9, D7, D6, -1, -1, 240, 240, 0},
  {"C2 HMI soft swap", D7, D9, D8, D6, -1, -1, 240, 240, 0},
  {"C3 HMI soft alt", D8, D9, D6, D7, -1, -1, 240, 240, 0},
  {"C4 SPI shared D14 D6 D7", D14, D6, D16, D15, D7, D8, 240, 240, 0},
  {"C5 SPI shared HMI", D6, D7, D16, D15, D8, D9, 240, 240, 0},
  {"C6 SPI shared alt", D8, D7, D16, D15, D6, D9, 240, 240, 0},
  {"C7 135x240 HMI", D8, D9, D7, D6, -1, -1, 135, 240, 1},
  {"C8 135x240 shared", D6, D7, D16, D15, D8, D9, 135, 240, 1},
};

const size_t CANDIDATE_COUNT = sizeof(candidates) / sizeof(candidates[0]);

void setPotentialBacklights(bool on) {
  int pins[] = {D6, D7, D8, D9, D14, D15, D16};
  for (int pin : pins) {
    pinMode(pin, OUTPUT);
  }
  if (on) {
    digitalWrite(D6, HIGH);
    digitalWrite(D7, HIGH);
    digitalWrite(D8, HIGH);
    digitalWrite(D9, HIGH);
  }
}

void drawCandidate(size_t index) {
  Candidate c = candidates[index];
  Serial.printf("Trying %s: cs=%d dc=%d mosi=%d sclk=%d rst=%d bl=%d size=%ux%u rot=%u\n", c.name, c.cs, c.dc, c.mosi, c.sclk, c.rst, c.bl, c.w, c.h, c.rotation);

  if (c.bl >= 0) {
    pinMode(c.bl, OUTPUT);
    digitalWrite(c.bl, HIGH);
  }

  Adafruit_ST7789 tft = Adafruit_ST7789(c.cs, c.dc, c.mosi, c.sclk, c.rst);
  tft.init(c.w, c.h);
  tft.setRotation(c.rotation);

  uint16_t bg = ST77XX_BLACK;
  if (index % 3 == 0) bg = ST77XX_BLUE;
  if (index % 3 == 1) bg = ST77XX_GREEN;
  if (index % 3 == 2) bg = ST77XX_RED;

  tft.fillScreen(bg);
  tft.setTextWrap(true);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(3);
  tft.setCursor(12, 18);
  tft.print("NIMO");
  tft.setTextSize(2);
  tft.setCursor(12, 62);
  tft.print("SCREEN TEST");
  tft.setCursor(12, 96);
  tft.print("C");
  tft.print(index + 1);
  tft.setTextSize(1);
  tft.setCursor(12, 132);
  tft.print(c.name);
  tft.setCursor(12, 156);
  tft.print("If visible, tell Cascade C");
  tft.print(index + 1);
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("Nimo screen scan starting...");
  Serial.printf("Board pins D6=%d D7=%d D8=%d D9=%d D14=%d D15=%d D16=%d\n", D6, D7, D8, D9, D14, D15, D16);
  setPotentialBacklights(true);
}

void loop() {
  static size_t index = 0;
  drawCandidate(index);
  index = (index + 1) % CANDIDATE_COUNT;
  delay(5000);
}
