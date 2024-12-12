#include <Arduino.h>
#include "Wire.h"
#include "defines.h"

float *sht3x(int addr)
{
    static float tempHum[2] = {0, 0}; // con "static" il valore rimane in memoria e possiamo ritornarlo come un puntatore (in questo caso un array)
    unsigned int byteTemp;
    unsigned int byteHum;

    Wire.beginTransmission(addr);
    Wire.write(0x2C); // comandi in byte per leggere temperatura e umidità
    Wire.write(0x06);
    Wire.endTransmission();
    delay(500);

    if (Wire.requestFrom(addr, 6) == 6) // legge 6 bytes, i byte 3 e 6 sono valori di checksum
    {
        byteTemp = Wire.read() << 8 | Wire.read(); // i primi due bytes sono il valore della temperatura
        Wire.read();                               // lettura a vuoto perchè quello che legge è un byte di checksum
        byteHum = Wire.read() << 8 | Wire.read();  // il byte 4 e 5 sono quelli del valore di umidità
    }
    else
    {
        tempHum[0] = 0xFFFE;
        tempHum[1] = 0xFFFE;
        return tempHum;
    }

    tempHum[0] = (-45.0 + 175.0 * ((float)byteTemp / 65535.0)) * 10; // Temperatura in °C
    tempHum[1] = (100.0 * ((float)byteHum / 65535.0)) * 10;          // Umidità relativa in %

#if DEBUG
    Serial.print("Valore temperatura: ");
    Serial.print(tempHum[0]);
    Serial.print(" Valore umidità: ");
    Serial.print(tempHum[1]);
    Serial.println();
#endif
    return tempHum; // ritorna un puntatore ma che può esser letto come un array
}