#include <Arduino.h>
#include <avr/wdt.h>
#include "Wire.h"
#include "defines.h"

unsigned long int misura_peso(int pin, int clock, int offset)
{
    byte data[3] = {0, 0, 0};
    unsigned long risultato16bit;

    // digitalWrite(powerSens, 0);
    pinMode(pin, INPUT);    // pin dout
    pinMode(clock, OUTPUT); // pin slk

    while (digitalRead(pin) && millis() % 5100 > 5000) // stai fermo per 5 secondi
        ;
    // wdt_reset();          // resetta il timer del watchdog
    for (int i = 3; i--;) // raccoglie i bit per il valore di peso grezzo
    {
        for (char j = 8; j--;)
        {
            digitalWrite(clock, 1);
            delayMicroseconds(30);
            data[i] |= (digitalRead(pin) << j);
            digitalWrite(clock, 0);
            delayMicroseconds(30);
        }
    }

    digitalWrite(clock, 1);
    digitalWrite(clock, 0);

    risultato16bit = ((long)data[2] << 8) | (long)data[1]; // tira fuori il risultato, tronca il bit meno significativo

    if (risultato16bit == 0b1111111111111111 || risultato16bit < 10)
        return 0;

#if DEBUG
    Serial.print("Peso: ");
    Serial.print(risultato16bit);
    Serial.println();
#endif
    if (offset != 0) // se il valore ritornato non è di taratura, ritorna il peso in grammi
        return ((risultato16bit - offset) * SCALA_PESO) / 2;
    else
        return risultato16bit;
}