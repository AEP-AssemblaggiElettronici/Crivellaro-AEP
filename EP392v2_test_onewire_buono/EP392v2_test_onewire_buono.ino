#include <OneWire.h>

byte init_1wire(OneWire &device, byte addr[], byte tipoSensore) {
  if (!device.search(addr)) {
    Serial.println("No more addresses.");
    Serial.println();
    device.reset_search();
    delay(250);
    return 0;
  }

  Serial.print("ROM =");
  for (int i = 0; i < 8; i++) {
    Serial.write(' ');
    Serial.print(addr[i], HEX);
  }

  if (OneWire::crc8(addr, 7) != addr[7]) {
    Serial.println("CRC is not valid!");
    return 0;
  }
  Serial.println();

  switch (addr[0]) {
    case 0x10:
      Serial.println("  Chip = DS18S20");  // or old DS1820
      tipoSensore = 1;
      break;
    case 0x28:
      Serial.println("  Chip = DS18B20");
      tipoSensore = 0;
      break;
    case 0x22:
      Serial.println("  Chip = DS1822");
      tipoSensore = 0;
      break;
    default:
      Serial.println("Device is not a DS18x20 family device.");
      return 0;
  }

  device.reset();
  device.select(addr);
  device.write(0x44, 1);  // start conversion, with parasite power on at the end

  delay(1000);  // maybe 750ms is enough, maybe not
  // we might do a ds.depower() here, but the reset will take care of it.

  return 1;
}

float read_1wire(OneWire &device, byte addr[], byte tipoSensore) {
  float celsius, fahrenheit;
  byte i;
  byte present = 0;
  byte data[9];

  present = device.reset();
  device.select(addr);
  device.write(0xBE);  // Read Scratchpad

  Serial.print("  Data = ");
  Serial.print(present, HEX);
  Serial.print(" ");
  for (i = 0; i < 9; i++) {  // we need 9 bytes
    data[i] = device.read();
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.print(" CRC=");
  Serial.print(OneWire::crc8(data, 8), HEX);
  Serial.println();

  // Convert the data to actual temperature
  // because the result is a 16 bit signed integer, it should
  // be stored to an "int16_t" type, which is always 16 bits
  // even when compiled on a 32 bit processor.
  int16_t raw = (data[1] << 8) | data[0];
  if (tipoSensore) {
    raw = raw << 3;  // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00)
      raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20)
      raw = raw & ~3;  // 10 bit res, 187.5 ms
    else if (cfg == 0x40)
      raw = raw & ~1;  // 11 bit res, 375 ms
                       //// default is 12 bit resolution, 750 ms conversion time
  }
  celsius = (float)raw / 16.0;
  fahrenheit = celsius * 1.8 + 32.0;
  Serial.print("  Temperature = ");
  Serial.print(celsius);
  Serial.print(" Celsius, ");
  Serial.print(fahrenheit);
  Serial.println(" Fahrenheit");

  return celsius;
}

OneWire sens1wire(20);
byte indirizzo1wire[8];
byte tipo1wire;
unsigned int cont = 0;

void setup() {
  Serial.begin(115200);
  pinMode(22, OUTPUT);
  digitalWrite(22, 1);
}

void loop() {
  for (int i = 0; i < 10; i++) {
    if (init_1wire(sens1wire, indirizzo1wire, tipo1wire)) {
      read_1wire(sens1wire, indirizzo1wire, tipo1wire);
      break;
    }
    delay(500);
  }

  Serial.println(cont);
  Serial.println("________________________________________________________________________________");
  delay(1000);
  cont++;
}
