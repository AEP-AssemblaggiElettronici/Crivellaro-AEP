#include <Arduino.h>

#define LED1 5
#define LED2 25
#define LED3 26
#define LED4 27

short int s = -1;
unsigned short int pins[4] = { LED1, LED2, LED3, LED4 };

void setup()
{
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(LED4, OUTPUT);

  Serial.begin(9600);
}

void loop()
{
  s++;
  delay(1000);;
  Serial.println(s);

  digitalWrite(pins[s], 1);
  delay(250);
  digitalWrite(pins[s], 0);
  
  if (s > 2) s = -1;
}