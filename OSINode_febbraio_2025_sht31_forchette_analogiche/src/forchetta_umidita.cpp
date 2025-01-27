#include "forchetta_umidita.h"
#include "defines.h"
#include <Arduino.h>
#include <avr/wdt.h>

unsigned int forchetta_umidita(int porta)
{
    pinMode(PORT_C_J_1_5, INPUT);
    pinMode(PORT_D_J_4_5, INPUT);
    int umidita = 0;
    int media = 0;
    word result = 0;

    // sensore umidità
    umidita = analogRead(porta);

    // calcola la media di 10 letture
    for (int i = 0; i < 10; i++)
    {
        umidita = analogRead(porta);
        media += umidita;
        delay(100);
    }

    result = media / 10;
    if (result >= 1000)
        result = 1000;
    /////

    wdt_reset();

#if DEBUG
    Serial.print("Sensore umidità terreno: ");
    Serial.print(result);
    Serial.println();
#endif
    return result;
}