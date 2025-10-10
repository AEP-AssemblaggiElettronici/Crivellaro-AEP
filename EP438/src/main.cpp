/*
LED Controller Arduino - Multiplo ID
Memorizza solo il payload corrispondente all'ID della scheda
*/

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <Adafruit_NeoPixel.h>

// === CONFIGURAZIONE ===
#define RXpin 8
#define TXpin 7
#define CONTROLPIN 10
#define L1 2
#define L2 3
#define L3 4

#define DIP1 A0
#define DIP2 A1
#define DIP3 A2
#define DIP4 A3
#define DIP5 A4
#define DIP6 A5

#define STX 0x02
#define MAX_PAYLOAD 1000 // 150

const uint8_t dips[] = {DIP1, DIP2, DIP3, DIP4, DIP5, DIP6};
uint8_t id = 0;
uint8_t stripLength = 5;

SoftwareSerial ser(RXpin, TXpin);
Adafruit_NeoPixel led1(5, L1, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel led2(5, L2, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel led3(5, L3, NEO_GRB + NEO_KHZ800);

enum State
{
    WAIT_STX,
    WAIT_ID,
    WAIT_CMD,
    WAIT_DATA,
    WAIT_CHK
};
State state = WAIT_STX;

uint8_t cmd, chk, rxChk, rxID;
uint8_t buffer[MAX_PAYLOAD];
uint8_t idx;
uint8_t idx_total;
bool hasPayload = false;

void readID()
{
    id = 0;
    for (int i = 0; i < 6; i++)
        id |= (digitalRead(dips[i]) << i);
    id ^= 0b111111;
}

void updateLEDs()
{
    if (!hasPayload)
    {
        Serial.println("ERRORE: Nessun payload valido!");
        return;
    }

    Serial.println("=== PAYLOAD RICEVUTO ===");

    led1.clear();
    led2.clear();
    led3.clear();

    uint8_t led_idx = 0;
    for (uint8_t strip = 0; strip < 3; strip++) // per ogni strip
    {
        for (uint8_t i = 0; i < stripLength; i++) // per ogni LED nella strip
        {
            uint8_t h = buffer[led_idx++];
            uint8_t s = buffer[led_idx++];
            uint8_t v = buffer[led_idx++];

            switch (strip)
            {
            case 0:
                led1.setPixelColor(i, led1.ColorHSV(h * 257, s, v));
                break;
            case 1:
                led2.setPixelColor(i, led2.ColorHSV(h * 257, s, v));
                break;
            case 2:
                led3.setPixelColor(i, led3.ColorHSV(h * 257, s, v));
                break;
            }
        }
    }

    led1.show();
    led2.show();
    led3.show();
    Serial.println("✓ LED aggiornati!");
}

void setup()
{
    for (int i = 0; i < 6; i++)
        pinMode(dips[i], INPUT_PULLUP);

    pinMode(CONTROLPIN, OUTPUT);
    digitalWrite(CONTROLPIN, LOW);

    Serial.begin(9600);
    ser.begin(9600);

    led1.begin();
    led1.clear();
    led1.updateLength(stripLength);
    led2.begin();
    led2.clear();
    led2.updateLength(stripLength);
    led3.begin();
    led3.clear();
    led3.updateLength(stripLength);

    readID();
    Serial.print("ID dispositivo: ");
    Serial.println(id);
    Serial.println("In attesa di comandi seriali...");
}

void loop()
{
    readID(); // aggiorna ID ad ogni ciclo

    while (ser.available())
    {
        uint8_t b = ser.read();

        switch (state)
        {
        case WAIT_STX:
            if (b == STX)
            {
                chk = 0;
                idx = 0;
                idx_total = 0;
                state = WAIT_ID;
            }
            break;

        case WAIT_ID:
            rxID = b;
            chk ^= b;
            Serial.print("ID attuale: ");
            Serial.println(id);
            Serial.print("ID ricevuto: ");
            Serial.println(b);
            state = WAIT_CMD;
            break;

        case WAIT_CMD:
            cmd = b;
            chk ^= b;
            if (cmd == 0x00) // STORE
            {
                idx = 0;
                state = WAIT_DATA;
            }
            else if (cmd == 0x02) // APPLY
            {
                idx = 0; // sicurezza
                state = WAIT_CHK;
            }
            else
            {
                state = WAIT_STX;
            }
            break;

        case WAIT_DATA:
            if (rxID == id)
            {
                buffer[idx++] = b; // salvo solo se l'ID corrisponde
            }
            chk ^= b; // il checksum va sempre aggiornato

            if (++idx_total >= stripLength * 9) // idx_total scorre sempre il pacchetto
                state = WAIT_CHK;

            if (idx >= MAX_PAYLOAD) // protezione overflow
            {
                Serial.println("Buffer overflow!");
                state = WAIT_STX;
            }
            break;

        case WAIT_CHK:
            rxChk = b;
            Serial.print("Checksum calcolato: ");
            Serial.println(chk, HEX);
            Serial.print("Checksum ricevuto: ");
            Serial.println(rxChk, HEX);

            if (chk == rxChk)
            {
                if (cmd == 0x00) // STORE
                {
                    if (rxID == id)
                    {
                        hasPayload = true;
                        Serial.println("✓ PAYLOAD MEMORIZZATO");
                    }
                    else
                    {
                        Serial.println("- Payload per altro dispositivo, ignorato");
                    }
                }
                else if (cmd == 0x02) // APPLY
                {
                    if (hasPayload)
                    {
                        digitalWrite(CONTROLPIN, HIGH);
                        updateLEDs();
                        digitalWrite(CONTROLPIN, LOW);
                    }
                    else
                    {
                        Serial.println("✗ APPLY senza payload valido!");
                    }
                }
            }
            else
            {
                Serial.println("✗ CHECKSUM ERRATO!");
            }
            state = WAIT_STX;
            break;
        }
    }
}
