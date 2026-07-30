#include <Arduino.h>
#include <Wire.h>
#include <circuit_kit.h>

using namespace ozobot::circuit_kit;

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
  Wire.endTransmission();
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

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("Nimo HMI bus probe starting...");
  Serial.printf("Pins D6=%d D7=%d D8=%d D9=%d RGB_LED=%d SDA=%d SCL=%d\n", D6, D7, D8, D9, RGB_LED, SDA, SCL);

  Wire.begin();
  ozobot::circuit_kit::Init();
  delay(500);

  scanCurrentBus("default before mux");
  for (uint8_t ch = 0; ch < 8; ch++) {
    selectMuxChannel(ch);
    char label[24];
    snprintf(label, sizeof(label), "mux channel %u", ch);
    scanCurrentBus(label);
  }

  probeHmiDescription();
  Serial.println("Probe done. RGB blinking purple.");
}

void loop() {
  static bool on = false;
  on = !on;
  neopixelWrite(RGB_LED, on ? 36 : 0, 0, on ? 36 : 0);
  delay(600);
}
