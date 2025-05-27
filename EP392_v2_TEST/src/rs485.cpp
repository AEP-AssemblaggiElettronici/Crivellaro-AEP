#include <Arduino.h>
#include "defines.h"

uint16_t rs485(Uart &device, bool tempHum)
{
    uint16_t risultato[11];
    uint16_t res;

    digitalWrite(PIN_TX_ENABLE, 1);
    delay(1);
    device.write(umiditaTemperatura, 8);
    delay(20);
    digitalWrite(PIN_TX_ENABLE, 0);
    delay(500);

    if (device.available())
    {
        for (int i = 0; i < 11; i++)
        {
            risultato[i] = device.read();
            Serial.print(risultato[i], HEX);
            Serial.print("|");
        }
        Serial.println();
        if (tempHum)
        {
            res = (risultato[3] << 8) | risultato[4];
            Serial.print("Umidità rs485: ");
            Serial.print(res / 10, DEC);
            Serial.println();
            return res;
        }
        else
        {
            // Serial.println((risultato[5] << 8) | risultato[6]);
            res = ((risultato[3] << 8) | risultato[4]) / 2;
            Serial.print("Temperatura rs485: ");
            Serial.print((((risultato[5] << 8) | risultato[6]) * 2) / 10, DEC);
            Serial.println();
            return res; // nella piattaforma va rimoltiplicato per due (e  riceverà solo il lo-byte)
        }
    }
    else
    {
        Serial.println("Sensore temperatura/umidità terreno RS485 non disponibile");
        return 0;
    }
}

// return (((bytes[0] * 256.0) + bytes[1]) / 10);