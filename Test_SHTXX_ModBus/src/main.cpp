#include <Arduino.h>
#include <ModbusRTUSlave.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include "SHT2x.h"

#define RXPIN 10
#define TXPIN 11
#define DRIVER_ENABLE_PIN 2
#define BAUD 9600
#define SLAVE_ID 1

SHT2x sht;
SoftwareSerial mySerial(RXPIN, TXPIN);
ModbusRTUSlave modbus(mySerial, DRIVER_ENABLE_PIN);
uint16_t registri[2];
uint16_t byteTemp;
uint16_t byteHum;
uint8_t addr = 40;

void setup()
{
  Wire.begin();
  sht.begin();
  Serial.begin(BAUD);
  modbus.configureHoldingRegisters(registri, 2);
  modbus.begin(SLAVE_ID, BAUD);
  delay(150);
}

void loop()
{
  sht.read(); // questo metodo va chiamato prima di fare le letture
  registri[0] = sht.getRawTemperature();
  registri[1] = sht.getRawHumidity();
  modbus.poll();
  delay(400);
}