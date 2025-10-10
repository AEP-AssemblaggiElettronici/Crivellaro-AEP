/* #include "Wire.h"
#include <Arduino.h>

void i2c_write(unsigned char device, unsigned char toAddress, unsigned char val) // scrive bytes su dispositivo I²C
{
    Wire.beginTransmission(device);
    Wire.write(toAddress);
    Wire.write(val);
    Wire.endTransmission();
}

void i2c_read(unsigned char device, unsigned char fromAddress, int num, unsigned char result[]) // legge bytes da dispositivo I²C
{
    Wire.beginTransmission(device);
    Wire.write(fromAddress);
    Wire.endTransmission();
    Wire.requestFrom((int)device, num);
    int i = 0;
    unsigned long start = millis();
    while (i < num && millis() - start < 50)
    { // timeout 50ms
        while (Wire.available())
        {
            result[i] = Wire.read();
            i++;
        }
    }
} */

#include "i2cUtils.h"

// Libera SDA/SCL se rimasti bloccati
bool i2c_bus_clear()
{
    pinMode(I2C_SCL_PIN, OUTPUT);
    pinMode(I2C_SDA_PIN, INPUT_PULLUP);

    if (digitalRead(I2C_SDA_PIN) == HIGH)
    {
        pinMode(I2C_SCL_PIN, INPUT);
        pinMode(I2C_SDA_PIN, INPUT);
        return true;
    }

    // genera fino a 9 clock manuali
    for (int i = 0; i < 9; ++i)
    {
        digitalWrite(I2C_SCL_PIN, HIGH);
        delayMicroseconds(5);
        digitalWrite(I2C_SCL_PIN, LOW);
        delayMicroseconds(5);
        if (digitalRead(I2C_SDA_PIN) == HIGH)
        {
            pinMode(I2C_SCL_PIN, INPUT);
            pinMode(I2C_SDA_PIN, INPUT);
            return true;
        }
    }

    // genera STOP
    pinMode(I2C_SDA_PIN, OUTPUT);
    digitalWrite(I2C_SDA_PIN, LOW);
    digitalWrite(I2C_SCL_PIN, HIGH);
    delayMicroseconds(5);
    digitalWrite(I2C_SDA_PIN, HIGH);
    delayMicroseconds(5);

    pinMode(I2C_SCL_PIN, INPUT);
    pinMode(I2C_SDA_PIN, INPUT);

    return (digitalRead(I2C_SDA_PIN) == HIGH);
}

// Riavvia Wire
void i2c_reinit()
{
    Wire.end();
    delay(5);
    Wire.begin();
#if defined(WIRE_HAS_TIMEOUT)
    Wire.setTimeout(I2C_READ_TIMEOUT_MS);
#endif
    delay(2);
}

// Scrittura con retry
void i2c_write(uint8_t device, uint8_t toAddress, uint8_t val)
{
    for (int attempt = 0; attempt < I2C_RETRIES; ++attempt)
    {
        Wire.beginTransmission(device);
        Wire.write(toAddress);
        Wire.write(val);
        uint8_t err = Wire.endTransmission();

        if (err == 0)
        {
#if I2C_DEBUG
            Serial.print("i2c_write OK dev 0x");
            Serial.println(device, HEX);
            Serial.print("address: ");
            Serial.println(toAddress, HEX);
#endif
            return;
        }

#if I2C_DEBUG
        Serial.print("i2c_write ERR ");
        Serial.print(err);
        Serial.print(" attempt ");
        Serial.println(attempt + 1);
#endif

        i2c_bus_clear();
        i2c_reinit();
    }

#if I2C_DEBUG
    Serial.println("i2c_write FAILED after retries");
#endif
}

// Lettura con timeout e retry
void i2c_read(uint8_t device, uint8_t fromAddress, int num, uint8_t result[])
{
    for (int attempt = 0; attempt < I2C_RETRIES; ++attempt)
    {
        Wire.beginTransmission(device);
        Wire.write(fromAddress);
        uint8_t err = Wire.endTransmission();
        if (err != 0)
        {
#if I2C_DEBUG
            Serial.print("i2c_read: endTransmission err ");
            Serial.println(err);
#endif
            i2c_bus_clear();
            i2c_reinit();
            continue;
        }

        Wire.requestFrom((int)device, num);

        unsigned long start = millis();
        int i = 0;
        while (i < num && (millis() - start) < I2C_READ_TIMEOUT_MS)
        {
            while (Wire.available() && i < num)
            {
                result[i++] = Wire.read();
            }
        }

        if (i == num)
        {
#if I2C_DEBUG
            Serial.print("i2c_read OK dev 0x");
            Serial.println(device, HEX);
            Serial.print("address: ");
            Serial.println(fromAddress, HEX);
#endif
            return;
        }

#if I2C_DEBUG
        Serial.print("i2c_read incomplete, got ");
        Serial.println(i);
#endif

        i2c_bus_clear();
        i2c_reinit();
    }

    for (int j = 0; j < num; ++j)
        result[j] = 0; // fallback
#if I2C_DEBUG
    Serial.println("i2c_read FAILED after retries");
#endif
}
