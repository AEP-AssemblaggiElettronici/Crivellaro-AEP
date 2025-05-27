#include <Wire.h>

uint8_t *sht3x(int addr);
bool i2c_scan_sht3x(TwoWire dispositivo);
void splitFloat(float number, uint8_t & integerPart, uint8_t & decimalPart);