#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#define LCD_MOSI 47
#define LCD_SCLK 21
#define LCD_CS 2
#define LCD_DC 3
#define LCD_RST -1

Adafruit_ST7789 tft = Adafruit_ST7789(LCD_CS, LCD_DC, LCD_MOSI, LCD_SCLK, LCD_RST);

const int BACKLIGHT_CANDIDATES[] = {1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 38, 39, 40, 41, 42, 45, 46, 48};
const size_t BACKLIGHT_COUNT = sizeof(BACKLIGHT_CANDIDATES) / sizeof(BACKLIGHT_CANDIDATES[0]);

void initExpander() {
  Serial.println("Initializing XL9555 expander over I2C (SDA=4, SCL=5)...");
  Wire.begin(4, 5); // SDA=GPIO4, SCL=GPIO5
  delay(100);
  
  for (uint8_t addr = 0x20; addr <= 0x27; addr++) {
    // Configure XL9555 Port 1 (P1_0=MUTE, P1_1=BACKLIGHT) as Output (0)
    Wire.beginTransmission(addr);
    Wire.write(0x07); // Configuration Register for Port 1
    Wire.write(0x00); // All pins of Port 1 set to Output
    uint8_t err = Wire.endTransmission();
    
    if (err == 0) {
      Serial.printf("Found XL9555 expander at 0x%02X! Configuring P1_0 and P1_1 as outputs.\n", addr);
      // Set outputs to HIGH initially
      Wire.beginTransmission(addr);
      Wire.write(0x03); // Output Port 1 Register
      Wire.write(0xFF); // Set all pins HIGH
      Wire.endTransmission();
    }
  }
}

void toggleExpander(bool high) {
  for (uint8_t addr = 0x20; addr <= 0x27; addr++) {
    Wire.beginTransmission(addr);
    Wire.write(0x03); // Output Port 1 Register
    Wire.write(high ? 0xFF : 0x00);
    Wire.endTransmission();
  }
}

void setBacklightCandidates(bool on) {
  for (size_t i = 0; i < BACKLIGHT_COUNT; i++) {
    pinMode(BACKLIGHT_CANDIDATES[i], OUTPUT);
    digitalWrite(BACKLIGHT_CANDIDATES[i], on ? HIGH : LOW);
  }
}

void drawScreen(uint16_t width, uint16_t height, uint8_t rotation, const char* label, uint16_t color) {
  Serial.printf("Trying %s size=%ux%u rotation=%u\n", label, width, height, rotation);
  setBacklightCandidates(true);
  tft.init(width, height);
  tft.setRotation(rotation);
  tft.fillScreen(color);
  tft.setTextWrap(true);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(3);
  tft.setCursor(12, 20);
  tft.print("Nimo");
  tft.setCursor(12, 58);
  tft.print("Online");
  tft.setTextSize(2);
  tft.setCursor(12, 105);
  tft.print(label);
  tft.setTextSize(1);
  tft.setCursor(12, 145);
  tft.print("MOSI47 SCLK21 CS2 DC3");
  tft.setCursor(12, 165);
  tft.print("If visible, tell Cascade.");
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("Nimo Hiwonder screen test starting...");
  Serial.printf("LCD pins: MOSI=%d SCLK=%d CS=%d DC=%d RST=%d\n", LCD_MOSI, LCD_SCLK, LCD_CS, LCD_DC, LCD_RST);
  initExpander();
  setBacklightCandidates(true);
}

void loop() {
  static bool toggle = false;
  toggle = !toggle;
  setBacklightCandidates(toggle);
  toggleExpander(toggle);
  Serial.printf("Backlight candidates and Expander set to %s\n", toggle ? "HIGH" : "LOW");
  
  tft.init(240, 320);
  tft.setRotation(1);
  tft.fillScreen(toggle ? ST77XX_BLUE : ST77XX_RED);
  tft.setTextWrap(true);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(3);
  tft.setCursor(12, 40);
  tft.print("Nimo Test");
  tft.setTextSize(2);
  tft.setCursor(12, 100);
  tft.printf("BL Pin: %s", toggle ? "HIGH (Blue)" : "LOW (Red)");
  
  delay(1000);
}
