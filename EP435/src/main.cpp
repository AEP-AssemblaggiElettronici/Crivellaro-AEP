#include <Arduino.h>
#include <Wire.h>
#include "sam.h"

#define LED 19
#define BAUD 9600
#define AD5693_ADDRESS 0x4C
#define WSEN2513130820302_ADDRESS 0x78

uint8_t valoreDigitaleL;
uint8_t valoreDigitaleH;
bool firstCycle = 0;
bool io = 0;

bool Wire_init(uint8_t);
void TC3_Handler();
void timer_setup();

void setup()
{
    Serial.begin(BAUD);
    Wire.begin();
    pinMode(LED, OUTPUT);
    delay(5000);
}

void loop()
{
    if (!firstCycle)
    {
        Serial.println("Controllo connessione dispositivi..");
        if (Wire_init(AD5693_ADDRESS) && Wire_init(WSEN2513130820302_ADDRESS))
            timer_setup();
        else
            return;
    }
    firstCycle = 1;

    // qui codice per leggere il valore analogico del sensore di pressione WSEN-2513130820302...
    Wire.beginTransmission(WSEN2513130820302_ADDRESS);
    Wire.requestFrom(WSEN2513130820302_ADDRESS, 2);
    if (Wire.available() == 2) {
      valoreDigitaleH = Wire.read();
      valoreDigitaleL = Wire.read();
    }
    Wire.endTransmission();
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

        Wire.beginTransmission(AD5693_ADDRESS);
        Wire.write((valoreDigitaleH >> 8) & 0xFF);
        Wire.write(valoreDigitaleL & 0xFF);
        Wire.endTransmission();

        Serial.println(valoreDigitaleH, HEX);
        Serial.println(valoreDigitaleL, HEX);
        Serial.println("____");

        digitalWrite(LED, !digitalRead(LED));
    }
}

void timer_setup()
{
    // 1️⃣ Abilita il clock per TC3 nel Generic Clock Controller (GCLK)
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN |
                        GCLK_CLKCTRL_GEN_GCLK0 |      // Generatore di clock 0 (48 MHz)
                        GCLK_CLKCTRL_ID(TC3_GCLK_ID); // ID TC3
    while (GCLK->STATUS.bit.SYNCBUSY)
        ; // Attendi sincronizzazione

    // 2️⃣ Abilita TC3 nel Power Manager (PM)
    PM->APBCMASK.bit.TC3_ = 1;

    // 3️⃣ Configura il Timer TC3: Prescaler 1024, modalità 16-bit, Wave Generation in modalità match
    TC3->COUNT16.CTRLA.reg = TC_CTRLA_MODE_COUNT16 |      // 16-bit mode
                             TC_CTRLA_PRESCALER_DIV1024 | // Prescaler 1024
                             TC_CTRLA_WAVEGEN_MFRQ;       // Modalità match
    while (TC3->COUNT16.STATUS.bit.SYNCBUSY)
        ; // Attendi sincronizzazione

    // 4️⃣ Imposta il valore di confronto per 5ms
    TC3->COUNT16.CC[0].reg = (48000000 / 1024) * 0.005; // ≈ 234
    while (TC3->COUNT16.STATUS.bit.SYNCBUSY)
        ;

    // 5️⃣ Abilita l'interrupt
    TC3->COUNT16.INTENSET.reg = TC_INTENSET_MC0; // Interrupt su match CC[0]
    NVIC_EnableIRQ(TC3_IRQn);                    // Nome corretto per TC3

    // 6️⃣ Avvia il timer
    TC3->COUNT16.CTRLA.reg |= TC_CTRLA_ENABLE;
    while (TC3->COUNT16.STATUS.bit.SYNCBUSY)
        ;
    Serial.println("Timer settato.");
}