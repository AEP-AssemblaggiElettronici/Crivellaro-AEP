#include <Arduino.h>
#include "defines.h"

long misura_peso(int pin, int clock)
{
    byte data[3] = {0, 0, 0};
    long rawData;

    pinMode(pin, INPUT);    // pin dout
    pinMode(clock, OUTPUT); // pin slk
    digitalWrite(clock, 1);
    delay(1);
    digitalWrite(clock, 0);

    delay(1000);
    if (digitalRead(pin))
    {
        Serial.print("Sensore di peso non presente su pin ");
        Serial.print(pin);
        Serial.println();
        return -1;
    }

    digitalWrite(clock, 0);

    for (int i = 3; i--;) // raccoglie i bit per il valore di peso grezzo
    {
        for (char j = 8; j--;)
        {
            digitalWrite(clock, 1);
            delayMicroseconds(1);
            data[i] |= (digitalRead(pin) << j);
            digitalWrite(clock, 0);
            delayMicroseconds(1);
        }
    }

    digitalWrite(clock, 1);
    delayMicroseconds(1);
    digitalWrite(clock, 0);
    delayMicroseconds(1);

    rawData = ((long)data[2] << 8) | (long)data[1]; // tira fuori il risultato, tronca il bit meno significativo

    return rawData;
}

long pesa(int pin, int clk, long offset)
{
    long misurazioni[10];
    unsigned long rawData = 0;

    for (int i = 0; i < 10; i++)
    {
        misurazioni[i] = misura_peso(pin, clk);
        delay(200);
        if (misurazioni[i] == -1) // se non è presente il sensore di peso, ferma la lettura della media
            return 0;
    }

    for (int i = 0; i < 10; i++) // ordina descrescente i valori letti
    {
        for (int j = 0; j < 10 - i - 1; j++)
        {
            unsigned long temp;
            if (misurazioni[i] > misurazioni[j])
            {
                temp = misurazioni[i];
                misurazioni[i] = misurazioni[j];
                misurazioni[j] = temp;
            }
        }
    }

    for (int i = 1; i < 9; i++)
        rawData += misurazioni[i];

    rawData /= 8;

    Serial.print("Peso (byte): ");
    Serial.print(rawData, HEX);
    Serial.println();
    if (offset)
    {
        Serial.print("Peso taratura (byte): ");
        Serial.print(offset, HEX);
        Serial.println();
    }
    Serial.print("Peso netto (byte): ");
    Serial.print(rawData - offset < 0 ? 0 : rawData - offset, HEX);
    Serial.println();
    /*     Serial.print("Peso (grammi): ");
        Serial.print((rawData - offset) * SCALA_PESO);
        Serial.println(); */

    if (offset)
    {
        rawData -= offset;
        if (rawData <= 0)
            return 0;
        else
            return rawData;
    }
    else
        return rawData;
}

long converti_peso(long peso)
{
    Serial.print("Peso in grammi: ");
    Serial.print((peso <= 0 ? 0 : peso * SCALA_PESO) * 2);
    Serial.println();

    return peso <= 0 ? 0 : (peso * SCALA_PESO);
}

long filtro_anti_zero(long attuale, long precedente)
{
    if (attuale == 0)
    {
        if (attuale == precedente)
            return 0;
        return precedente;
    }
    return attuale;
}