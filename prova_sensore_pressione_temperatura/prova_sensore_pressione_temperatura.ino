#include "Wire.h"

#define WSEN2513130820302_ADDRESS 0x78
#define BAUD 115200

uint8_t pressioneH;
uint8_t pressioneL;
uint8_t temperaturaH;
uint8_t temperaturaL;

void setup() {
  Serial.begin(BAUD);
  Wire.begin();
  delay(5000);
}

void loop() {
  // qui codice per leggere il valore analogico del sensore di pressione WSEN-2513130820302...
  Wire.beginTransmission(WSEN2513130820302_ADDRESS);
  Wire.requestFrom(WSEN2513130820302_ADDRESS, 4);
  if (Wire.available() == 4) {
    pressioneH = Wire.read();
    pressioneL = Wire.read();
    temperaturaH = Wire.read();
    temperaturaL = Wire.read();
  }
  Wire.endTransmission();

  Serial.println(pressioneH, HEX);
  Serial.println(pressioneL, HEX);
  Serial.println(temperaturaH, HEX);
  Serial.println(temperaturaL, HEX);
  Serial.println(".....................");
  delay(1000);
}
