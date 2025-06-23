#include <Arduino.h>
#include "defines.h"

void rs485(Uart &device, uint8_t array[])
{
    for (int i = 10; i--;)
    {
        digitalWrite(PIN_TX_ENABLE, 1);
        delay(1);
        device.write(umiditaTemperatura, 8);
        delay(20);
        digitalWrite(PIN_TX_ENABLE, 0);
        device.flush();
        delay(500);

        if (i < 8)
        {
            if (device.available() > 0)
            {
                for (int i = 0; i < 11; i++)
                {
                    array[i] = device.read();
                    Serial.print(array[i], HEX);
                    Serial.print("|");
                }
                Serial.println();
                Serial.print("Temperatura rs485: ");
                Serial.print(((array[5] << 8) | array[6]) / 10);
                Serial.println();
                Serial.print("Umidità rs485: ");
                Serial.print(((array[3] << 8) | array[4]) / 10);
                Serial.println();
                return;
            }
            else
            {
                Serial.println("Sensore temperatura/umidità terreno RS485 non disponibile");
            }
        }
    }
    return;
}

// vecchia funziona:
/* uint16_t rs485(Uart &device, bool temp1Hum0)
{
    uint8_t risultato[11];
    uint16_t res;

    for (int i = 10; i--;)
    {
        digitalWrite(PIN_TX_ENABLE, 1);
        delay(1);
        device.write(umiditaTemperatura, 8);
        delay(20);
        digitalWrite(PIN_TX_ENABLE, 0);
        device.flush();
        delay(500);

        if (i < 8)
        {
            if (device.available() > 0)
            {
                for (int i = 0; i < 11; i++)
                {
                    risultato[i] = device.read();
                    Serial.print(risultato[i], HEX);
                    Serial.print("|");
                }
                Serial.println();
                if (!temp1Hum0)
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
                    res = ((risultato[5] << 8) | risultato[6]) * 2;
                    Serial.print("Temperatura rs485: ");
                    Serial.print(res / 20, DEC);
                    Serial.println();
                    return res;
                }
            }
            else
            {
                Serial.println("Sensore temperatura/umidità terreno RS485 non disponibile");
            }
        }
    }
    return 0;
} */

// return (((bytes[0] * 256.0) + bytes[1]) / 10);