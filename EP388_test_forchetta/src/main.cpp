#include <Arduino.h>
#include "SoftwareSerial.h"

#define RX 16
#define TX 17
#define TX_ENABLE 33
#define POW 18
#define BAUD 9600
#define FORKETT_BAUD 4800

const byte umiditaTemperatura[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x03, 0x05, 0xCB};
EspSoftwareSerial::UART forkett;
byte valori[11];

float prendiDati(bool);

void setup()
{
  pinMode(RX, INPUT);
  pinMode(TX, OUTPUT);
  pinMode(POW, OUTPUT);
  pinMode(TX_ENABLE, OUTPUT);

  digitalWrite(POW, 1);

  forkett.begin(FORKETT_BAUD, SWSERIAL_8N1, RX, TX, false);
  Serial.begin(BAUD);
}

void loop()
{
  memset(valori, 0, sizeof(valori));
  delay(250);
  Serial.printf("Umidità: %.2f\n", prendiDati(0));
  delay(1000);
  Serial.printf("Temperatura: %.2f\n", prendiDati(1));
}

float prendiDati(bool hum0Temp1)
{
  float result;
  digitalWrite(TX_ENABLE, 1);
  delay(50);
  if (forkett.write(umiditaTemperatura, sizeof(umiditaTemperatura)) == 8)
  {
    digitalWrite(TX_ENABLE, 0);
    for (byte i = 0; i < 11; i++)
    {
      // Serial.print(mod.read(),HEX);
      valori[i] = forkett.read();
      // Serial.println(valori[i], HEX);
    }
    if (hum0Temp1)
      return (((valori[5] * 256.0) + valori[6]) / 10); // converting hexadecimal to decimal

    return (((valori[3] * 256.0) + valori[4]) / 10); // converting hexadecimal to decimal
  }
}