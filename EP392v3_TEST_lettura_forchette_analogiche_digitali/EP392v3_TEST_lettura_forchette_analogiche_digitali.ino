#include <HardwareSerial.h>

#define PIN_FORK_C 4
#define PIN_FORK_C_PRESENZA 14
#define PIN_FORK_D 1
#define PIN_FORK_D_PRESENZA 46
#define BOOST_SHTDWN 19
#define BOOST_EN 20
#define RS485_DE 17
#define RS485_RE 18
#define RS485_IN_C 14
#define RS485_IN_D 46
#define RS485_TX_1 12
#define RS485_RX_1 13
#define RS485_TX_2 15
#define RS485_RX_2 16
#define FORKETT_BAUD 4800

HardwareSerial radio(1);
HardwareSerial forkett(2);

const byte umiditaTemperatura[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x03, 0x05, 0xCB };

bool forchettePresenti = 0;

void rs485(HardwareSerial &device, uint8_t array[]) {
  digitalWrite(RS485_DE, 1);
  digitalWrite(RS485_RE, 1);
  delay(250);
  device.write(umiditaTemperatura, 8);
  device.flush();
  digitalWrite(RS485_RE, 0);
  digitalWrite(RS485_DE, 0);
  delay(10);

  if (device.available() > 0) {
    for (int i = 0; i < 11; i++) {
      array[i] = device.read();
      Serial.print(array[i], HEX);
      Serial.print("|");
    }
    Serial.println();
    Serial.print("Temperatura rs485: ");
    Serial.println((array[5] << 8) | array[6], HEX);
    Serial.println(((array[5] << 8) | array[6]) / 10);
    Serial.print("Umidità rs485: ");
    Serial.println((array[3] << 8) | array[4], HEX);
    Serial.println(((array[3] << 8) | array[4]) / 10);
    return;
  } else {
    Serial.println("Sensore temperatura/umidità terreno RS485 non disponibile");
    return;
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  pinMode(PIN_FORK_C_PRESENZA, INPUT_PULLUP);
  pinMode(PIN_FORK_D_PRESENZA, INPUT_PULLUP);
  Serial.println("Rilevamento forchette su porte C e D (o E e F)");
  uint8_t tentativiPresenzaForchetta = 0;
  while (tentativiPresenzaForchetta < 10) {
    if (!digitalRead(PIN_FORK_C_PRESENZA) || !digitalRead(PIN_FORK_D_PRESENZA)) {
      Serial.println("Forchette presenti sulle porte indicate");
      forchettePresenti = 1;
      break;
    }
    delay(100);
    tentativiPresenzaForchetta++;
  }
  if (tentativiPresenzaForchetta == 10) Serial.println("Forchette non presenti sulle porte indicate");

  pinMode(PIN_FORK_C, INPUT_PULLUP);
  pinMode(PIN_FORK_D, INPUT_PULLUP);

  pinMode(BOOST_SHTDWN, OUTPUT);
  pinMode(RS485_DE, OUTPUT);
  pinMode(RS485_RE, OUTPUT);

  digitalWrite(BOOST_SHTDWN, 0);
}

void loop() {
  uint16_t forchettaAnalogC = 0xFFFE;
  uint16_t forchettaAnalogD = 0xFFFE;
  uint8_t rs485risultati[11] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  uint8_t rs485risultati2[11] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  uint16_t rs485TempE = 0xFFFE;
  uint16_t rs485HumE = 0xFFFE;
  uint16_t rs485TempF = 0xFFFE;
  uint16_t rs485HumF = 0xFFFE;

  digitalWrite(BOOST_SHTDWN, 1);
  digitalWrite(BOOST_EN, 1);
  delay(200);
  Serial.println("Lettura forchetta RS485 su porta E:");
  forkett.begin(FORKETT_BAUD, SERIAL_8N1, RS485_RX_2, RS485_TX_2);
  delay(10);
  rs485(forkett, rs485risultati);
  if (((rs485risultati[5] << 8) | rs485risultati[6]) == 0xFFFF) {
    Serial.print("Forchetta RS485 non presente, lettura valore forchetta umidità su porta C: ");
    uint32_t mediaForchetta = 0;
    for (int i = 10; i--;) {
      mediaForchetta += analogRead(PIN_FORK_C);
    }
    Serial.println(mediaForchetta);  // DEBUG
    forchettaAnalogC = mediaForchetta / 10;
    forchettaAnalogC >>= 2;  // eliminiamo i 2 bit meno significativi (è una divisione per 4)
    if (forchettaAnalogC > 1000)
      forchettaAnalogC = 1000;
    Serial.print(forchettaAnalogC);
    Serial.println();
  } else {
    rs485TempE = (rs485risultati[5] << 8) | rs485risultati[6];
    rs485HumE = (rs485risultati[3] << 8) | rs485risultati[4];
  }
  delay(250);

  Serial.println("Lettura forchetta RS485 su porta F:");
  forkett.begin(FORKETT_BAUD, SERIAL_8N1, RS485_RX_1, RS485_TX_1);
  delay(10);
  rs485(forkett, rs485risultati2);
  if (((rs485risultati2[5] << 8) | rs485risultati2[6]) == 0xFFFF) {
    Serial.print("Forchetta RS485 non presente, lettura  forchetta su porta D: ");
    uint32_t mediaForchetta = 0;
    for (int i = 10; i--;) {
      mediaForchetta += analogRead(PIN_FORK_D);
    }
    Serial.println(mediaForchetta);  // DEBUG
    forchettaAnalogD = mediaForchetta / 10;
    forchettaAnalogD >>= 2;  // eliminiamo i 2 bit meno significativi (è una divisione per 4)
    if (forchettaAnalogD > 1000)
      forchettaAnalogD = 1000;
    Serial.print(forchettaAnalogD);
    Serial.println();
  } else {
    rs485TempF = (rs485risultati2[5] << 8) | rs485risultati2[6];
    rs485HumF = (rs485risultati2[3] << 8) | rs485risultati2[4];
  }
  digitalWrite(BOOST_SHTDWN, 0);
  delay(1000);

  digitalWrite(RS485_DE, 0);
  digitalWrite(RS485_RE, 1);
  esp_sleep_enable_timer_wakeup(5ULL * 1000000ULL);
  esp_light_sleep_start();
}
