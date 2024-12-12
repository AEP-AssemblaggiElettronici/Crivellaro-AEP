#include <Arduino.h>
#include "defines.h"
#include "Wire.h"

unsigned int luxmetro(int indirizzo)
{
    int i = 0;
    int buffer[2];
    int errorCheck;
    word result = 0;

    pinMode(PORT_A, OUTPUT);
    pinMode(PORT_B, OUTPUT);
    digitalWrite(PORT_A, 1);
    digitalWrite(PORT_B, 1);
    delay(150);

    Wire.beginTransmission(indirizzo);
    Wire.write(0x41); // 4x reolution 120ms
    Wire.endTransmission();
    delay(100);

    Wire.beginTransmission(indirizzo);
    Wire.write(0x6F); // 4x reolution 120ms
    Wire.endTransmission();
    delay(100);

    Wire.beginTransmission(indirizzo);
    Wire.write(0x13); // 4x reolution 120ms
    Wire.endTransmission();
    delay(200);

    Wire.beginTransmission(indirizzo);
    Wire.requestFrom(indirizzo, 2);
    while (Wire.available()) //
    {
        buffer[i] = Wire.read(); // receive one byte
        i++;
    }
    Wire.endTransmission();

    digitalWrite(PORT_A, 0);
    digitalWrite(PORT_B, 0);

    if (i == 2)
    {
        Wire.beginTransmission(indirizzo);
        errorCheck = Wire.endTransmission();

        if (!errorCheck)
            // combina i due byte in un numero solo (sposta a sinistra il byte alto e aggiunge con un OR i byte basso):
            result = (word)(((buffer[0] << 8) | buffer[1]) / 1.2);
#if DEBUG
        Serial.print("Valore luxmetro: ");
        Serial.print(result);
        Serial.println();
#endif
        return result;
    }

    return 65534;
}