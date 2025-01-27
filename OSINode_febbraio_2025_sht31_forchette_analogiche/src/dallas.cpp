#include <Arduino.h>
#include "defines.h"

bool reset(uint8_t pin)
{
    pinMode(pin, OUTPUT);
    digitalWrite(pin, 0);
    delayMicroseconds(480);
    pinMode(pin, INPUT_PULLUP);
    delayMicroseconds(70);

    // Controlla se c'è una risposta dal sensore
    bool presence = !digitalRead(pin);
    delayMicroseconds(410);
    return presence;
}

void writeBit(uint8_t pin, bool bit)
{
    pinMode(pin, OUTPUT);
    digitalWrite(pin, 0);
    delayMicroseconds(bit ? 10 : 60);
    digitalWrite(pin, 1);
    delayMicroseconds(bit ? 55 : 5);
}

bool readBit(uint8_t pin)
{
    pinMode(pin, OUTPUT);
    digitalWrite(pin, 0);
    delayMicroseconds(2);
    pinMode(pin, INPUT);
    delayMicroseconds(8);
    bool bit = digitalRead(pin);
    delayMicroseconds(50);
    return bit;
}

void writeByte(uint8_t pin, uint8_t byte)
{
    for (int i = 0; i < 8; i++)
    {
        writeBit(pin, byte & 0x01); // Scrive il bit meno significativo per primo
        byte >>= 1;
    }
}

uint8_t readByte(uint8_t pin)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++)
        byte |= (readBit(pin) << i); // Legge il bit meno significativo per primo
    return byte;
}

word dallasRead(uint8_t pin)
{
    word result = 65534;
    if (reset(pin))
    {                         // Reset e verifica se il sensore risponde
        writeByte(pin, 0xCC); // Skip ROM
        writeByte(pin, 0xBE); // Read Scratchpad

        uint8_t loB = readByte(pin); // Primo byte
        uint8_t hiB = readByte(pin); // Secondo byte

        result = (hiB << 8) | loB; // Unisce i due byte

        return result;
    }
#if DEBUG
    if (result == 65534)
        Serial.println("Sensore Dallas non presente");
    else
    {
        Serial.print("Sensore Dallas: ");
        Serial.print(result);
        Serial.println();
    }
#endif
    return result;
}