#include "Wire.h"

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
    while (Wire.available())
    {
        result[i] = Wire.read();
        i++;
    }
}