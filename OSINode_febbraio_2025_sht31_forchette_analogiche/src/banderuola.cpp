#include <Arduino.h>
#include <avr/wdt.h>
#include "defines.h"

word banderuola()
{
    pinMode(PORT_D_J_4_5, INPUT);
    wdt_enable(WDTO_8S);

    word grado = 65534;
    float voltaggio;
    int valoreD = 0;

    for (int i = 0; i <= 9; i++)
    {
        valoreD += analogRead(PORT_D_J_4_5); // la banderuola funziona SOLO nella porta D!
        delay(10);
    }

    voltaggio = float(valoreD / 1024.00 * 5.00);

    if (voltaggio <= 4.78 + 0.3 && voltaggio >= 4.78 - 0.1)
        grado = 3150;
    if (voltaggio <= 4.62 + 0.06 && voltaggio >= 4.62 - 0.2)
        grado = 2700;
    if (voltaggio <= 4.04 + 0.38 && voltaggio >= 4.04 - 0.10)
        grado = 2925;
    if (voltaggio <= 3.84 + 0.1 && voltaggio >= 3.84 - 0.21)
        grado = 0;
    if (voltaggio <= 3.43 + 0.2 && voltaggio >= 3.43 - 0.23)
        grado = 3375;
    if (voltaggio <= 3.08 + 0.12 && voltaggio >= 3.08 - 0.08)
        grado = 2250;
    if (voltaggio <= 2.93 + 0.07 && voltaggio >= 2.93 - 0.43)
        grado = 2475;
    if (voltaggio <= 2.25 + 0.25 && voltaggio >= 2.25 - 0.15)
        grado = 450;
    if (voltaggio <= 1.98 + 0.12 && voltaggio >= 1.98 - 0.28)
        grado = 225;
    if (voltaggio <= 1.40 + 0.3 && voltaggio >= 1.40 - 0.1)
        grado = 1800;
    if (voltaggio <= 1.19 + 0.11 && voltaggio >= 1.19 - 0.19)
        grado = 2025;
    if (voltaggio <= 0.90 + 0.1 && voltaggio >= 0.90 - 0.1)
        grado = 1350;
    if (voltaggio <= 0.62 + 0.18 && voltaggio >= 0.62 - 0.07)
        grado = 1575;
    if (voltaggio <= 0.45 + 0.1 && voltaggio >= 0.45 - 0.02)
        grado = 900;
    if (voltaggio <= 0.41 + 0.02 && voltaggio >= 0.41 - 0.06)
        grado = 675;
    if (voltaggio <= 0.32 + 0.02 && voltaggio >= 0.32 - 0.32)
        grado = 1125;

#if DEBUG
    if (grado == 65534)
        Serial.println("Direzione del vento non presente");
    else
    {
        Serial.print("Direzione del vento: ");
        Serial.print(grado);
        Serial.println();
    }
#endif
    return grado;
}