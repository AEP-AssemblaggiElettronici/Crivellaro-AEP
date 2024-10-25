#include <Wire.h>
#include "Adafruit_SHT31.h"

#define FORK A2

void setup() {
  pinMode(FORK, INPUT);
  Serial.begin(9600);
}

void loop() {
  Serial.println(analogRead(FORK));
  delay(500);
}
