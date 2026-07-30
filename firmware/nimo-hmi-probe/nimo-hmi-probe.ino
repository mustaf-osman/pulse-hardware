#include <Wire.h>
#include <circuit_kit.h>

using namespace ozobot::circuit_kit;

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("Nimo HMI probe starting...");
  Serial.printf("Pins: D6=%d D7=%d D8=%d D9=%d RGB_LED=%d SDA=%d SCL=%d\n", D6, D7, D8, D9, RGB_LED, SDA, SCL);

  Wire.begin(SDA, SCL);
  ozobot::circuit_kit::Init();
  delay(500);

  Serial.println("Selecting HMI module...");
  CommunicateWith(HMI);
  delay(300);
  Serial.printf("HMI pins: gpio0=%d gpio1=%d gpio2=%d gpio3=%d\n", HMI.gpio_0, HMI.gpio_1, HMI.gpio_2, HMI.gpio_3);

  auto description = GetSensorDescription(HMI);
  if (!description) {
    Serial.println("HMI description: not present or invalid");
  } else {
    Serial.println("HMI description:");
    Serial.print(ToString(description.get()).c_str());
  }

  pinMode(RGB_LED, OUTPUT);
  Serial.println("Probe finished. RGB will blink blue.");
}

void loop() {
  static bool on = false;
  on = !on;
  neopixelWrite(RGB_LED, 0, 0, on ? 48 : 0);
  delay(600);
}
