/* void i2c_write(unsigned char, unsigned char, unsigned char);
void i2c_read(unsigned char, unsigned char, int, unsigned char[]); */

#ifndef I2CUTILS_H
#define I2CUTILS_H

#include <Arduino.h>
#include <Wire.h>

// Debug su Serial (1=attivo, 0=disattivo)
#define I2C_DEBUG 1

// Numero di retry in caso di errore
#define I2C_RETRIES 3

// Timeout per le letture (ms)
#define I2C_READ_TIMEOUT_MS 50

// Pin default per bus I²C su AVR (cambia se usi altro MCU)
#define I2C_SDA_PIN A4
#define I2C_SCL_PIN A5

// API compatibili col tuo codice attuale
void i2c_write(uint8_t device, uint8_t toAddress, uint8_t val);
void i2c_read(uint8_t device, uint8_t fromAddress, int num, uint8_t result[]);

// Recovery manuale del bus
bool i2c_bus_clear();

// Reinizializza Wire
void i2c_reinit();

#endif
