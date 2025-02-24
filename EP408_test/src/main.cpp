#include <Arduino.h>

// la notazione è quella dell'adafruit feather m0
#define RELAY1 15
#define RELAY2 16
#define RELAY3 23
#define RELAY4 24

const uint8_t relays[] = {RELAY1, RELAY2, RELAY3, RELAY4};

void setup()
{
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);
  pinMode(RELAY4, OUTPUT);
}

void loop()
{
  for (uint8_t i = 0; i < 4; i++)
  {
    if (i != 0)
    {
      digitalWrite(relays[i], 1);
      digitalWrite(relays[i - 1], 0);
    }
    else
    {
      digitalWrite(relays[i], 1);
      digitalWrite(relays[(int)(sizeof(relays) / sizeof(relays[0]))], 0);
    }
    delay(1000);
  }
}