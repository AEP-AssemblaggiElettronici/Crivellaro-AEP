#define BOOST_SHTDWN 19
#include <HardwareSerial.h>

#define BOOST_EN 20
#define PIN_FORK_C 4
#define PIN_FORK_C_PRESENZA 14
#define PIN_FORK_D 6
#define PIN_FORK_D_PRESENZA 46
#define RS485_TX_1 12
#define RS485_RX_1 13
#define RS485_TX_2 15
#define RS485_RX_2 16
#define RS485_DE 17
#define RS485_RE 18
#define FORKETT_BAUD 4800

const byte umiditaTemperatura[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x03, 0x05, 0xCB };

HardwareSerial forkett(2);

// -----------------------------------------------------------
// Lettura robusta da sensore RS485 con timeout e quiete
// -----------------------------------------------------------
void rs485(HardwareSerial &device, uint8_t array[]) {
  // pulizia buffer prima della nuova lettura
  while (device.available()) device.read();

  digitalWrite(RS485_DE, 1);
  digitalWrite(RS485_RE, 1);
  delay(5);

  device.write(umiditaTemperatura, 8);
  device.flush();

  digitalWrite(RS485_RE, 0);
  digitalWrite(RS485_DE, 0);

  unsigned long start = millis();
  int idx = 0;

  // attendi fino a 300 ms per la risposta
  while (millis() - start < 300) {
    if (device.available()) {
      array[idx++] = device.read();
      if (idx >= 11) break;
    }
  }

  if (idx < 7) {
    Serial.println("⚠️ Nessuna risposta RS485");
    for (int i = 0; i < 11; i++) array[i] = 0xFF;
  } else {
    Serial.print("RX [");
    for (int i = 0; i < idx; i++) {
      Serial.print(array[i], HEX);
      Serial.print("|");
    }
    Serial.println("]");
  }

  // tempo di quiete prima della prossima query
  delay(200);
}

void setup() {
  Serial.begin(115200);
  pinMode(BOOST_EN, OUTPUT);
  pinMode(BOOST_SHTDWN, OUTPUT);
  pinMode(RS485_DE, OUTPUT);
  pinMode(RS485_RE, OUTPUT);
}

void loop() {
  uint16_t forchettaAnalogC = 0xFFFE;
  uint16_t forchettaAnalogD = 0xFFFE;
  uint8_t rs485risultati[11] = { 0 };
  uint8_t rs485risultati2[11] = { 0 };
  uint16_t rs485TempE = 0xFFFE;
  uint16_t rs485HumE = 0xFFFE;
  uint16_t rs485TempF = 0xFFFE;
  uint16_t rs485HumF = 0xFFFE;
  uint32_t accumuloTempE = 0;
  uint32_t accumuloHumE = 0;
  uint32_t accumuloTempF = 0;
  uint32_t accumuloHumF = 0;

  digitalWrite(BOOST_SHTDWN, 1);
  digitalWrite(BOOST_EN, 1);
  delay(200);

  // ---------------- Porta E ----------------
  Serial.println("Lettura forchetta RS485 su porta E:");
  forkett.begin(FORKETT_BAUD, SERIAL_8N1, RS485_RX_1, RS485_TX_1);
  delay(10);
  for (int j = 0; j < 10; j++) {
    rs485(forkett, rs485risultati);
    if (rs485risultati[0] != 0xFF) {
      accumuloTempE += ((rs485risultati[5] << 8) | rs485risultati[6]) / 10;
      accumuloHumE += ((rs485risultati[3] << 8) | rs485risultati[4]) / 10;
    }
  }
  if (((rs485risultati[5] << 8) | rs485risultati[6]) == 0xFFFF) {
    Serial.print("Forchetta RS485 non presente, lettura valore forchetta umidità su porta C: ");
    uint32_t mediaForchetta = 0;
    for (int i = 0; i < 10; i++) {
      mediaForchetta += analogRead(PIN_FORK_C);
      delay(50);
    }
    forchettaAnalogC = (mediaForchetta / 10) >> 2;
    if (forchettaAnalogC > 1000) forchettaAnalogC = 1000;
    Serial.println(forchettaAnalogC);
  } else {
    rs485TempE = accumuloTempE / 10;
    rs485HumE = accumuloHumE / 10;
    Serial.print("Temperatura E: ");
    Serial.println(rs485TempE);
    Serial.print("Umidità E: ");
    Serial.println(rs485HumE);
  }

  delay(300);

  // ---------------- Porta F ----------------
  Serial.println("Lettura forchetta RS485 su porta F:");
  forkett.begin(FORKETT_BAUD, SERIAL_8N1, RS485_RX_2, RS485_TX_2);
  delay(10);
  for (int j = 0; j < 10; j++) {
    rs485(forkett, rs485risultati2);
    if (rs485risultati2[0] != 0xFF) {
      accumuloTempF += ((rs485risultati2[5] << 8) | rs485risultati2[6]) / 10;
      accumuloHumF += ((rs485risultati2[3] << 8) | rs485risultati2[4]) / 10;
    }
  }
  if (((rs485risultati2[5] << 8) | rs485risultati2[6]) == 0xFFFF) {
    Serial.print("Forchetta RS485 non presente, lettura forchetta su porta D: ");
    uint32_t mediaForchetta = 0;
    for (int i = 0; i < 10; i++) {
      mediaForchetta += analogRead(PIN_FORK_D);
      delay(50);
    }
    forchettaAnalogD = (mediaForchetta / 10) >> 2;
    if (forchettaAnalogD > 1000) forchettaAnalogD = 1000;
    Serial.println(forchettaAnalogD);
  } else {
    rs485TempF = accumuloTempF / 10;
    rs485HumF = accumuloHumF / 10;
    Serial.print("Temperatura F: ");
    Serial.println(rs485TempF);
    Serial.print("Umidità F: ");
    Serial.println(rs485HumF);
  }

  // Disattiva RS485 e boost
  digitalWrite(RS485_RE, 1);
  digitalWrite(RS485_DE, 0);
  digitalWrite(BOOST_SHTDWN, 0);
  digitalWrite(BOOST_EN, 0);

  delay(2000);
}
