#include <Arduino.h>
#include <SPI.h>
#include "Adafruit_AD569x.h"
#include <math.h>

#define AMP_PIN 12

Adafruit_AD569x AD5963;
bool noDevice = 0;
bool firstCycle = 0;
uint16_t data;

void drawSine();
void deviceInit();

void setup()
{
  Serial.begin(9600);
}

void loop()
{
  if (!firstCycle)
  {
    deviceInit();
    firstCycle = 1;
  }
  if (!noDevice)
    drawSine();
}

void drawSine()
{
  // "mi è sembrato di vedere una sinusoide..." cit.
  for (float i = 0; i <= (PI * 2); i += 0.1)
  {
    data = (uint16_t)((sin(i) + 1) * (0xFFFF / 2));
    if (!AD5963.writeUpdateDAC(data))
    {
      Serial.print("Dato non inviato ad iterazione sinusoide #");
      Serial.print(i);
      Serial.println();
    }
  }
}

void deviceInit()
{
  if (AD5963.begin(0x4C, &Wire))
    Serial.println("AD5693 OK");
  else
  {
    Serial.println("AD5693 non connesso");
    noDevice = 1;
  }
  AD5963.reset();
  if (AD5963.setMode(NORMAL_MODE, true, true))
    Serial.println("AD5963 configurato");
  else
  {
    Serial.println("AD5693 non inizializzato");
    noDevice = 1;
  }
  Wire.setClock(800000);
}