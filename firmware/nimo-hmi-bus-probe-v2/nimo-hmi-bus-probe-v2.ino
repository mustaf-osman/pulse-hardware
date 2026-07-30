#include <Arduino.h>
#include <Wire.h>
#include <circuit_kit.h>

using namespace ozobot::circuit_kit;

int pinToGpio(int pin) {
  return digitalPinToGPIONumber(pin);
}

void printPins() {
  Serial.println("\nPin map:");
  int pins[] = {D0, D1, D2, D3, D4, D5, D6, D7, D8, D9, D14, D15, D16, D17, D18, D19, D20, D21, D22, D23, RGB_LED};
  const char* names[] = {"D0","D1","D2","D3","D4","D5","D6","D7","D8","D9","D14/SS","D15/SCK","D16/MOSI","D17/MISO","D18/SDA","D19/SCL","D20/IRQ","D21/TX","D22/RX","D23/BTN","RGB_LED"};
  for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
    Serial.printf("  %s digital=%d gpio=%d\n", names[i], pins[i], pinToGpio(pins[i]));
  }
}

void scanCurrentBus(const char* label) {
  Serial.printf("\nI2C scan: %s\n", label);
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.printf("  found 0x%02X\n", addr);
      found++;
    }
    delay(2);
  }
  if (!found) Serial.println("  no devices");
}

void selectMuxChannel(uint8_t channel) {
  Wire.beginTransmission(0x70);
  Wire.write(1 << channel);
  uint8_t err = Wire.endTransmission();
  Serial.printf("Select mux channel %u -> err=%u\n", channel, err);
  delay(80);
}

void probeHmiDescription() {
  Serial.println("\nProbe HMI via circuit_kit...");
  CommunicateWith(HMI);
  delay(200);
  auto description = GetSensorDescription(HMI);
  if (!description) {
    Serial.println("HMI description: not present or invalid");
    return;
  }
  Serial.println("HMI description:");
  Serial.print(ToString(description.get()).c_str());
}

void runProbe() {
  printPins();
  scanCurrentBus("default before mux");
  for (uint8_t ch = 0; ch < 8; ch++) {
    selectMuxChannel(ch);
    char label[24];
    snprintf(label, sizeof(label), "mux channel %u", ch);
    scanCurrentBus(label);
  }
  probeHmiDescription();
  Serial.println("\nProbe round done.");
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("Nimo HMI bus probe v2 starting...");
  Wire.begin(SDA, SCL);
  ozobot::circuit_kit::Init();
  delay(500);
  runProbe();
}

void loop() {
  static unsigned long last = 0;
  static bool on = false;
  on = !on;
  neopixelWrite(RGB_LED, on ? 36 : 0, 0, on ? 36 : 0);
  delay(500);
  if (millis() - last > 15000) {
    last = millis();
    Serial.println("\n--- repeating probe ---");
    runProbe();
  }
}
