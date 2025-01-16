#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
// #include <led_operations.h>

#define PIN 5
#define LEDS_NUMBER 64
#define BRIGHTNESS 0x80
#define SATURATION 0xFF
#define DEFAULT_COLOR_SHIFT 0xF0

Adafruit_NeoPixel leds(LEDS_NUMBER, PIN, NEO_GRB + NEO_KHZ800);
uint16_t colorr = 0;
uint8_t shapes[][18] = {
    {9, 10, 13, 14, 17, 18, 21, 22, 41, 42, 43, 44, 45, 46, 50, 51, 52, 53},
    {17, 18, 21, 22, 34, 35, 36, 37, 41, 42, 43, 44, 45, 46, 50, 51, 52, 53},
    {9, 10, 13, 14, 17, 18, 21, 22, 41, 42, 43, 44, 45, 46},
    {17, 18, 21, 22, 41, 42, 43, 44, 45, 46}};
uint8_t *blacks = shapes[0];
uint8_t blacksSize = 18;
bool mode = 0;
unsigned char colorShift = DEFAULT_COLOR_SHIFT;

void setup()
{
  leds.begin();
  leds.clear();
  Serial.begin(9600);
}

void loop()
{
  switch (Serial.read())
  {
  case ' ': // cambia modalità (colore fisso o arcobaleno)
    mode ^= 1;
    break;

  case '+': // aumenta velocità
    colorShift += 0x10;
    break;

  case '-': // diminuisce velocità
    colorShift -= 0x10;
    break;

  case 'a':
    blacks = shapes[0];
    blacksSize = 18;
    break;

  case 's':
    blacks = shapes[1];
    blacksSize = 18;
    break;

  case 'd':
    blacks = shapes[2];
    blacksSize = 14;
    break;

  case 'f':
    blacks = shapes[3];
    blacksSize = 10;
    break;
  }

  for (uint8_t i = 0; i < LEDS_NUMBER; i++)
  {
    bool nero = 0;
    uint16_t sfumatura = colorr + (i * 65536L / LEDS_NUMBER);
    for (uint8_t j = 0; j < blacksSize; j++)
      if (i == blacks[j])
      {
        nero = 1;
        break;
      }
    leds.setPixelColor(i, leds.ColorHSV(mode ? sfumatura : colorr, SATURATION, nero ? 0x0 : BRIGHTNESS));
  }

  colorr += colorShift;
  leds.show();

  if (colorr >= 0xFFFF)
    colorr = 0;
}