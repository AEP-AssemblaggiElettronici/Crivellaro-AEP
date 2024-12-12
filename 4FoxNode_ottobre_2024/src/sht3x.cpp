#include <Arduino.h>
#include "Wire.h"
#include "defines.h"

unsigned short int *sht3x(int addr)
{
    static unsigned short int tempHum[2] = {0, 0};
    delay(1000);

    Wire.beginTransmission(addr);
    Wire.write(0x2C); // comandi in byte per leggere temperatura e umidità
    Wire.write(0x06);
    Wire.endTransmission();
    delay(1000);

    if (Wire.requestFrom(addr, 6) == 6)
    {
        tempHum[0] = (uint8_t)((Wire.read() << 8 | Wire.read()) / 256); // convertiamo ad 8 il numero a 16 bit
        tempHum[1] = (uint8_t)((Wire.read() << 8 | Wire.read()) / 256);
    }
    else
    {
        tempHum[0] = 0xFE;
        tempHum[1] = 0xFE;
    }

#if DEBUG
    Serial.print("Valore temperatura: ");
    Serial.print(tempHum[0]);
    Serial.print(" Valore umidità: ");
    Serial.print(tempHum[1]);
    Serial.println();
#endif
    return tempHum; // ritorna un puntatore ma che può esser letto come un array
}