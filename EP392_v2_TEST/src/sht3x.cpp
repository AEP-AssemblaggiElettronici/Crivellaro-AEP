#include <Arduino.h>
#include "Wire.h"
#include "defines.h"

void splitFloat(float number, uint8_t &integerPart, uint8_t &decimalPart)
{
    // Estrai la parte intera
    integerPart = static_cast<int>(number);
    // Estrai la parte decimale
    decimalPart = static_cast<int>((number - integerPart) * 10);
}

uint8_t *sht3x(int addr)
{
    float tempHum[2] = {0, 0};       // 0 temperatura, 1 umidità
    static uint8_t ritornoValori[2]; // 0 temperatura, 1 umidità, con "static" il valore rimane in memoria e possiamo ritornarlo come un puntatore (in questo caso un array)
    unsigned int byteTemp;
    unsigned int byteHum;
    uint8_t humInt; // creo due variabili per la parte intera e decimale dell'umidità
    uint8_t humFloat;
    uint8_t tempInt;
    uint8_t tempFloat;

    Serial.println("Invio comandi di lettura SHT31..."); // DEBUG
    Wire.beginTransmission(addr);
    Wire.write(0x2C); // comandi in byte per leggere temperatura e umidità
    Wire.write(0x06);
    Wire.endTransmission();
    delay(500);

    Serial.println("Comando inviato, scansione temperatura SHT31..."); // DEBUG
    if (Wire.requestFrom(addr, 6) == 6)                                // legge 6 bytes, i byte 3 e 6 sono valori di checksum
    {
        byteTemp = Wire.read() << 8 | Wire.read(); // i primi due bytes sono il valore della temperatura
        Wire.read();                               // lettura a vuoto perchè quello che legge è un byte di checksum
        byteHum = Wire.read() << 8 | Wire.read();  // il byte 4 e 5 sono quelli del valore di umidità
    }
    else
    {
        uint8_t *ritornoNeutro;
        ritornoNeutro[0] = 0xFE; // ritorna 255 per temperatura e umidità se non è presente il sensore
        ritornoNeutro[1] = 0xFE;
        return ritornoNeutro;
    }

    tempHum[0] = ((-45.0 + 175.0 * ((float)byteTemp / 65535.0)) * 10.0f) / 10; // Temperatura in °C
    tempHum[1] = ((100.0 * ((float)byteHum / 65535.0)) * 10.0f) / 10;          // Umidità relativa in %

    Serial.print("Valore temperatura: ");
    Serial.print(tempHum[0]);
    Serial.print(" Valore umidità: ");
    Serial.print(tempHum[1]);
    Serial.println();

    splitFloat(tempHum[0], tempInt, tempFloat);
    splitFloat(tempHum[1], humInt, humFloat);

    if (tempFloat >= 8)
    {                  // CASO .8 e .9
        tempInt += 1;  // sale di un grado
        tempInt += 30; // mantenere valori non negativi
        tempInt *= 2;  // diventa un numero pari
    }
    else if (tempFloat <= 2)
    {                  // CASO .1 e .2
        tempInt += 30; // mantenere valori non negativi
        tempInt *= 2;  // diventa un numero pari
    }
    else
    {                  // CASO .3, .4, .5, .6,.7
        tempInt += 30; // mantenere valori non negativi
        tempInt *= 2;  // diventa un numero pari
        tempInt += 1;  // diventa un numero dispari
    }

    ritornoValori[0] = tempInt;

    if (humFloat >= 8)
    {                // CASO .8 e .9
        humInt += 1; // sale di un grado
        humInt *= 2; // diventa un numero pari
    }
    else if (humFloat <= 2) // su codice originale leggeva tempFloat, boh!
    {                       // CASO .1 e .2
        humInt *= 2;        // diventa un numero pari
    }
    else
    {                // CASO .3, .4, .5, .6,.7
        humInt *= 2; // diventa un numero pari
        humInt += 1; // diventa un numero dispari
    }

    if (humInt >= 1000)
        ritornoValori[1] = 256;
    else
        ritornoValori[1] = humInt;

    return ritornoValori; // ritorna un puntatore ma che può esser letto come un array
}

bool i2c_scan_sht3x(TwoWire dispositivo) // scan I2C per trovare i/l sensori/e
{
    byte address, error;
    for (address = 1; address < 127; address++)
    {
        dispositivo.beginTransmission(address);
        error = dispositivo.endTransmission();

        if (error == 0)
        {
            // Serial.println(address);
            if (address == SHT3X_ADDRESS)
            {
                Serial.println("Dispositivo SHT3X trovato");
                return 1;
            }
        }
    }
    Serial.println("Nessun dispositivo trovato.");
    return 0;
}