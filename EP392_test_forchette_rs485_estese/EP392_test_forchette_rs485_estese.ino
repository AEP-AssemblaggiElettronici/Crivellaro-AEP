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

const uint8_t comandoLetturaUniversale[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08 };
const uint8_t comandoCambioId[] = { 0x01, 0x06, 0x07, 0xD0, 0x00, 0x02, 0x08, 0x86 };

HardwareSerial forkett(2);

// -----------------------------------------------------------
// Lettura robusta da sensore RS485 con timeout e quiete
// -----------------------------------------------------------
void rs485_estesa(HardwareSerial &device, uint8_t array[], const uint8_t comando[]) {
  // pulizia buffer prima della nuova lettura
  while (device.available()) device.read();

  digitalWrite(RS485_DE, 1);
  digitalWrite(RS485_RE, 1);
  delay(5);

  device.write(comando, 8);
  device.flush();
  delay(5);

  digitalWrite(RS485_RE, 0);
  digitalWrite(RS485_DE, 0);

  unsigned long start = millis();
  int idx = 0;

  while (millis() - start < 1000) {
    if (device.available()) {
      array[idx++] = device.read();
      if (idx >= 19) break;
      delay(10);
    }
  }

  if (idx < 7) {
    Serial.println("⚠️ Nessuna risposta RS485");
    for (int i = 0; i < 19; i++) array[i] = 0xFF;
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
  uint8_t rs485risultati[19] = { 0 };

  digitalWrite(BOOST_SHTDWN, 1);
  digitalWrite(BOOST_EN, 1);
  delay(200);
  forkett.begin(FORKETT_BAUD, SERIAL_8N1, RS485_RX_2, RS485_TX_2);
  delay(10);
  rs485_estesa(forkett, rs485risultati, comandoLetturaUniversale);
  Serial.print("Umidità: ");
  Serial.print((float)((rs485risultati[3] << 8) | rs485risultati[4]) / 10);
  Serial.print("% Temperatura: ");
  Serial.print((float)((rs485risultati[5] << 8) | rs485risultati[6]) / 10);
  Serial.print("°C Conduttività: ");
  Serial.print((float)((rs485risultati[7] << 8) | rs485risultati[8]) / 10);
  Serial.print(" us/cm PH: ");
  Serial.print((float)((rs485risultati[9] << 8) | rs485risultati[10]) / 10);
  Serial.print(" N: ");
  Serial.print((float)((rs485risultati[11] << 8) | rs485risultati[12]) / 10);
  Serial.print(" mg/kg P: ");
  Serial.print((float)((rs485risultati[13] << 8) | rs485risultati[14]) / 10);
  Serial.print(" mg/kg K: ");
  Serial.print((float)((rs485risultati[15] << 8) | rs485risultati[16]) / 10);
  Serial.println(" mg/kg");

  // Disattiva RS485 e boost
  /*   digitalWrite(RS485_RE, 1);
  digitalWrite(RS485_DE, 0);
  digitalWrite(BOOST_SHTDWN, 0);
  digitalWrite(BOOST_EN, 0); */

  delay(3000);
}
