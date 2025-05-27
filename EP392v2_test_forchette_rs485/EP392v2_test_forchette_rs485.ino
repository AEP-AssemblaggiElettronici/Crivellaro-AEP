#include <Arduino.h>         // required before wiring_private.h
#include "wiring_private.h"  // pinPeripheral() function

#define forkett Serial1
#define forkett2 Serial5
#define RX 0
#define TX 1
#define RX2 31
#define TX2 30
#define TX_ENABLE 13
#define BAUD 115200
#define FORKETT_BAUD 4800
#define BOOST_EN 17      // se è su, alimenta i 4 volt su porte C e D, per le bilance
#define BOOST_SHTDWN 18  // se è su assieme a BOOST_EN, alimenta i 12 volt sulle porte C e D, per le forchette analogiche

const byte umiditaTemperatura[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x03, 0x05, 0xCB };

void setup() {
  pinMode(TX_ENABLE, OUTPUT);
  pinMode(BOOST_EN, OUTPUT);
  pinMode(BOOST_SHTDWN, OUTPUT);
  digitalWrite(BOOST_EN, 1);
  digitalWrite(BOOST_SHTDWN, 1);

  pinPeripheral(RX, PIO_SERCOM);
  pinPeripheral(TX, PIO_SERCOM);
  pinPeripheral(RX2, PIO_SERCOM_ALT);
  pinPeripheral(TX2, PIO_SERCOM_ALT);
  Serial.begin(BAUD);
  forkett.begin(FORKETT_BAUD);
  forkett2.begin(FORKETT_BAUD);
}

void loop() {
  digitalWrite(TX_ENABLE, 1);
  delay(1);
  forkett.write(umiditaTemperatura, 8);
  delay(20);  // 20
  digitalWrite(TX_ENABLE, 0);
  delay(500);  // 500
  if (forkett.available()) {
    for (int i = 0; i < 11; i++) {
      Serial.print(forkett.read(), HEX);
      Serial.print("|");
    }
    Serial.println();
  } else Serial.println("Sensore E non disponibile");

/*   digitalWrite(TX_ENABLE, 1);
  delay(1);
  forkett2.write(umiditaTemperatura, 8);
  delay(20);  // 20
  digitalWrite(TX_ENABLE, 0);
  delay(500);  // 500
  if (forkett2.available()) {
    for (int i = 0; i < 11; i++) {
      Serial.print(forkett2.read(), HEX);
      Serial.print("|");
    }
    Serial.println();
  } else Serial.println("Sensore F non disponibile"); */
}