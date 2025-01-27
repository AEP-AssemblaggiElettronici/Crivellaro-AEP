#include <Arduino.h>
#include "avr/wdt.h"
#include "defines.h"

unsigned int anemometro()
{
    word conteggioReturn = 0;
    unsigned int conteggio = 0;
    const int pin = 11;
    bool statoPin = 0;
    bool statoAttuale = 1;
    bool statoPrecedente = 1;
    unsigned long conteggioFinale = 0;

    pinMode(0, OUTPUT); // setto il pin 0 (RX) come output
    pinMode(1, OUTPUT); // setto il pin 1 (TX) come output
    digitalWrite(0, 0); // sets  (RX) off
    digitalWrite(1, 0); // sets  (TX) off

    for (int i = 0; i < 5; i = i + 1)
    {
        conteggioFinale = millis() + 5000; // calcola il tempo di fine conteggio degli impulsi
        digitalWrite(pin, 1);              // turn on pullup resistors  pin for LS count
        delay(1000);
        while (millis() < conteggioFinale)
        {
            wdt_reset();                 // resetta il timer del watchdog
            statoPin = digitalRead(pin); // read input value
            if (statoPin)                // check if the input is HIGH
                statoAttuale = 1;
            else
                statoAttuale = 0;

            if (statoAttuale != statoPrecedente)
                if (statoAttuale) // solo la transizione basso-alto è conteggiata
                    conteggio++;

            statoPrecedente = statoAttuale;
            delay(10);
        }

        if (conteggio > conteggioReturn)
            conteggioReturn = conteggio;
    }

    if (conteggio <= 0)
        conteggioReturn = 0xFFFE;

    digitalWrite(pin, 0); // turn off pullup resistors  pin for LS count - power saving

#if DEBUG
    if (conteggioReturn == 0xFFFE)
        Serial.println("Anemometro non presente");
    else
    {
        Serial.print("Anemometro: ");
        Serial.print(conteggioReturn);
        Serial.println();
    }
#endif
    return conteggioReturn;
}