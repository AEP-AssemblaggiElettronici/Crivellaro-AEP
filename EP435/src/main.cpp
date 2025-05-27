#include <Arduino.h>
#include <Wire.h>
#include "sam.h"

#define LED 19
#define BAUD 115200
#define AD5693_ADDRESS 0x4C
#define WSEN2513130820302_ADDRESS 0x78
#define LIMITE_PRESSIONE_MIN 0x0CCD
#define LIMITE_PRESSIONE_MAX 0x6666

uint8_t pressione[2];
uint8_t temperatura[2];
bool firstCycle = 0;
uint8_t stato = 0;
uint16_t bytesPressioneUniti;

bool Wire_init(uint8_t);
void TC3_Handler();
void timer_setup();
bool debug = 0;

void setup()
{
    Serial.begin(BAUD);
    Wire.begin();
    pinMode(LED, OUTPUT);
    delay(500);

    // Abilita il clock per il watchdog
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_WDT | GCLK_CLKCTRL_GEN_GCLK2 | GCLK_CLKCTRL_CLKEN;
    while (GCLK->STATUS.bit.SYNCBUSY)
        ;

    // Configura il watchdog per resettare il microcontrollore dopo circa 4 secondi
    WDT->CONFIG.reg = WDT_CONFIG_PER_1K; // 1096 cicli = ~1s
    WDT->CTRL.reg = WDT_CTRL_ENABLE;     // Abilita il watchdog
    while (WDT->STATUS.bit.SYNCBUSY)
        ;
}

void loop()
{
    // Resetta il watchdog
    WDT->CLEAR.reg = WDT_CLEAR_CLEAR_KEY;
    while (WDT->STATUS.bit.SYNCBUSY)
        ;

    if (!firstCycle)
    {
        Serial.println("Controllo connessione dispositivi..");
        if (Wire_init(AD5693_ADDRESS) && Wire_init(WSEN2513130820302_ADDRESS))
            timer_setup();
        else
            return;
    }
    firstCycle = 1;

    switch (stato)
    {
    case 0: // idle
        break;
    case 1: // lettura sensore
        Wire.beginTransmission(WSEN2513130820302_ADDRESS);
        Wire.requestFrom(WSEN2513130820302_ADDRESS, 4);
        if (Wire.available() == 4)
        {
            pressione[0] = Wire.read();
            pressione[1] = Wire.read();
            temperatura[0] = Wire.read();
            temperatura[1] = Wire.read();

            bytesPressioneUniti = (pressione[0] << 8) | pressione[1];
            if (bytesPressioneUniti < LIMITE_PRESSIONE_MIN)
                bytesPressioneUniti = LIMITE_PRESSIONE_MIN;
            if (bytesPressioneUniti > LIMITE_PRESSIONE_MAX)
                bytesPressioneUniti = LIMITE_PRESSIONE_MAX;
        }
        Wire.endTransmission();

        if (millis() % 1000 < 5) // stampa su seriale
        {
            Serial.print("Pressione: ");
            // Serial.println((bytesPressioneUniti >> 8) & 0xFF, HEX);
            // Serial.println(bytesPressioneUniti & 0xFF, HEX);
            Serial.print(bytesPressioneUniti, DEC);
            Serial.println();
            // Serial.println(temperatura[0], HEX);
            // Serial.println(temperatura[1], HEX);
            Serial.print("Temperatura: ");
            Serial.print((temperatura[0] << 8) | temperatura[1]);
            Serial.println();
            Serial.println(".....................");
        }

        digitalWrite(LED, 0);

        stato = 2;
        break;
    case 2: // scrittura DAC
        // prima uniamo i 2 bytes e poi li mappiamo secondi i valori limiti in ingresso presenti nel datasheet del sensore
        // min: 0x0CCD (3277) max: 0x6666 (26214) valore a vuoto: 0x4000 (16384)
        uint16_t bytesPressioneMappati = map(bytesPressioneUniti, LIMITE_PRESSIONE_MIN, LIMITE_PRESSIONE_MAX, 0x0000, 0xFFFE);

        /* if (millis() % 1000 < 10) // stampa su seriale
        {
            Serial.println("VALORI MAPPATI (pressione e temperatura):");
            // Serial.println((bytesPressioneMappati >> 8) & 0xFF, HEX);
            // Serial.println(bytesPressioneMappati & 0xFF, HEX);
            Serial.println(bytesPressioneMappati, DEC);
            // Serial.println(temperatura[0], HEX);
            // Serial.println(temperatura[1], HEX);
            Serial.println((temperatura[0] << 8) | temperatura[1], DEC);
            Serial.println(".....................");
        } */

        Wire.beginTransmission(AD5693_ADDRESS);
        Wire.write(0x10); // comando di scrittura
        Wire.write((bytesPressioneMappati >> 8) & 0xFF);
        Wire.write(bytesPressioneMappati & 0xFF);
        Wire.endTransmission();

        digitalWrite(LED, 1);

        stato = 0;
        break;
    }
}

bool Wire_init(uint8_t address)
{
    Wire.beginTransmission(address);
    if (Wire.endTransmission())
    {
        Serial.println(address == WSEN2513130820302_ADDRESS ? "WSEN-2513130820302 non connesso." : "AD9653 non connesso.");
        return 0;
    }
    return 1;
}

void TC3_Handler()
{
    if (TC3->COUNT16.INTFLAG.bit.MC0)
    {                                              // Controlla se è un match su CC0
        TC3->COUNT16.INTFLAG.reg = TC_INTFLAG_MC0; // Pulisci il flag di interrupt

        digitalWrite(LED, 1);
        if (!stato)
            stato = 1;
    }
}

void timer_setup()
{
    // Abilita il clock per TC3 nel Generic Clock Controller (GCLK)
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN |
                        GCLK_CLKCTRL_GEN_GCLK0 |      // Generatore di clock 0 (48 MHz)
                        GCLK_CLKCTRL_ID(TC3_GCLK_ID); // ID TC3
    while (GCLK->STATUS.bit.SYNCBUSY)
        ; // Attendi sincronizzazione

    // Abilita TC3 nel Power Manager (PM)
    PM->APBCMASK.bit.TC3_ = 1;

    // Configura il Timer TC3: Prescaler 1024, modalità 16-bit, Wave Generation in modalità match
    TC3->COUNT16.CTRLA.reg = TC_CTRLA_MODE_COUNT16 |      // 16-bit mode
                             TC_CTRLA_PRESCALER_DIV1024 | // Prescaler 1024
                             TC_CTRLA_WAVEGEN_MFRQ;       // Modalità match
    while (TC3->COUNT16.STATUS.bit.SYNCBUSY)
        ; // Attendi sincronizzazione

    // Imposta il valore di confronto per 5ms
    TC3->COUNT16.CC[0].reg = (48000000 / 1024) * 0.005; // ≈ 234
    while (TC3->COUNT16.STATUS.bit.SYNCBUSY)
        ;

    // Abilita l'interrupt
    TC3->COUNT16.INTENSET.reg = TC_INTENSET_MC0; // Interrupt su match CC[0]
    NVIC_EnableIRQ(TC3_IRQn);                    // Nome corretto per TC3

    // Avvia il timer
    TC3->COUNT16.CTRLA.reg |= TC_CTRLA_ENABLE;
    while (TC3->COUNT16.STATUS.bit.SYNCBUSY)
        ;
    Serial.println("Timer settato.");
}