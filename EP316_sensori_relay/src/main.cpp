#include <Arduino.h>

#define RELAY1 PD4
#define RELAY2 PD5
#define IN1 A0
#define IN2 A1

bool statoRelay1, statoRelay2;

void setup() {
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(IN1, INPUT);
  pinMode(IN2, INPUT);
}

void loop() {
  if (!digitalRead(IN1))
  {
    delay(50);
    if (!digitalRead(IN1)) statoRelay1 = 1;
    else statoRelay1 = 0;
  }

  if (!digitalRead(IN2))
  {
    delay(50);
    if (!digitalRead(IN2)) statoRelay2 = 1;
    else statoRelay2 = 0;
  }

  digitalWrite(RELAY1, statoRelay1);
  digitalWrite(RELAY2, statoRelay2);
}