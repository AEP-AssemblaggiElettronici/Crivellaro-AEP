#include <Arduino.h>
#include <avr/wdt.h>
#include "defines.h"

word pluviometro()
{
    static word conteggioInviato = 0;
    int conteggio = 0;
    int statoPin = 0; // variable for reading the pin status
    int statoAttuale = 1;
    int statoPrecedente = 1;
    unsigned long conteggioFinale;

    Serial.end();       // disabilito la seriale per togliere alimentazione sui pin 0 (RX) e 1 (TX)
    pinMode(0, OUTPUT); // setto il pin 0 (RX) come output
    pinMode(1, OUTPUT); // setto il pin 1 (TX) come output
    digitalWrite(0, 0); // sets  (RX) off
    digitalWrite(1, 0); // sets  (TX) off

    digitalWrite(PORT_B, 1); // turn on pullup resistors  pin for LS count
    delay(1000);
    conteggioFinale = millis() + 5000; // calcola il tempo di fine conteggio degli impulsi

    while (millis() < conteggioFinale)
    {
        wdt_reset();                    // resetta il timer del watchdog
        statoPin = digitalRead(PORT_B); // read input value
        if (statoPin)
            statoAttuale = 1; // check if the input is HIGH
        else
            statoAttuale = 0;

        if (statoAttuale != statoPrecedente)
            if (statoAttuale == 1) // solo la transizione basso-alto è conteggiata
                conteggio++;

        statoPrecedente = statoAttuale;
        delay(10);
    }

    conteggioInviato = conteggio; // restituisce il tasso di pioggia mm*ora

    if (conteggio <= 0)
        conteggioInviato = 65534;

    digitalWrite(PORT_B, 0); // turn off pullup resistors  pin for LS count - power saving
    Serial.begin(BAUD);      // ripristino la seriale

#if DEBUG
    if (conteggioInviato == 65534)
        Serial.println("Pluviometro non presente");
    else
    {
        Serial.print("Pluviometro: ");
        Serial.print(conteggioInviato);
        Serial.println();
    }
#endif
    return conteggioInviato;
}