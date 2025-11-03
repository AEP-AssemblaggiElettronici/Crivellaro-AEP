#include <SoftwareSerial.h>

#define DETECT 4
#define LED 13
#define DI 2
#define RO 3

SoftwareSerial seriale(DI, RO);

const uint8_t HEADER[4] = { 0xF4, 0xF3, 0xF2, 0xF1 };
const uint8_t FRAME_LEN = 23;

void setup() {
  pinMode(DETECT, INPUT);
  pinMode(LED, OUTPUT);
  Serial.begin(115200);
  seriale.begin(115200);
}

void loop() {
  // --- LED section ---
  if (digitalRead(DETECT)) {
    digitalWrite(LED, HIGH);
  } else {
    digitalWrite(LED, LOW);
  }

  // --- Serial section ---
  if (seriale.available() >= FRAME_LEN) {
    // Cerca l'header
    if (findHeader()) {
      uint8_t buffer[FRAME_LEN];
      // Header già letto, leggi il resto del pacchetto
      for (uint8_t i = 0; i < FRAME_LEN; i++) {
        while (!seriale.available());
        buffer[i] = seriale.read();
      }

      // Stampa il frame
      Serial.print("FRAME: ");
      for (uint8_t i = 0; i < FRAME_LEN; i++) {
        if (buffer[i] < 0x10) Serial.print("0");
        Serial.print(buffer[i], HEX);
        Serial.print(" ");
      }
      Serial.println();
    }
  }
}

bool findHeader() {
  static uint8_t state = 0;

  while (seriale.available()) {
    uint8_t b = seriale.read();

    if (b == HEADER[state]) {
      state++;
      if (state == 4) {
        state = 0;
        return true;  // header trovato
      }
    } else {
      // Se il byte non corrisponde, ma è 0xF4, riparti da 1
      state = (b == HEADER[0]) ? 1 : 0;
    }
  }
  return false;
}












#include <SoftwareSerial.h>

#define DEBUG 1
#define DETECT 4
#define LED 13
#define DI 2
#define RO 3
#define FRAME_LEN 23

SoftwareSerial seriale(DI, RO);

const uint8_t HEADER[4] = { 0xF4, 0xF3, 0xF2, 0xF1 };
enum Stati { CHECK_HEADER,
             RICEZIONE };

Stati stato = CHECK_HEADER;
uint8_t indiceHeader = 0;

void setup() {
  pinMode(DETECT, INPUT);
  pinMode(LED, OUTPUT);
  Serial.begin(115200);
  seriale.begin(115200);
}

void loop() {
#if DEBUG
  if (digitalRead(DETECT)) digitalWrite(LED, 1);
  else digitalWrite(LED, 0);
#endif

  if (seriale.available()) {
    uint8_t byteIn = seriale.read();
    switch (stato) {
      case CHECK_HEADER:
        switch (indiceHeader) {
          case 0:
            if (byteIn == HEADER[0]) indiceHeader++;
            break;
          case 1:
            if (byteIn == HEADER[1]) indiceHeader++;
            break;
          case 2:
            if (byteIn == HEADER[2]) indiceHeader++;
            break;
          case 3:
            if (byteIn == HEADER[3]) {
              indiceHeader = 0;
              stato = RICEZIONE;
            }
            break;
        }
        break;
      case RICEZIONE:
        uint8_t buffer[20];
        for (int i = 0; i < FRAME_LEN - 9; i++) {
          while (!seriale.available())
            ;
          buffer[i] = seriale.read();
        }

#if DEBUG
        for (int i = 0; i < FRAME_LEN - 9; i++) {
          Serial.print(buffer[i], HEX);
          Serial.print("|");
        }
        Serial.println();
#endif

        switch (buffer[3]) {
          case 0: Serial.print("No target "); break;
          case 1: Serial.print("Campaign target "); break;
          case 2: Serial.print("Stationary target "); break;
          case 3: Serial.print("Campaign & stationary target "); break;
        }
        Serial.print("- Movement target distance: ");
        Serial.print((buffer[5] << 8) | buffer[4]);
        Serial.print("cm - Exercise target energy value: ");
        Serial.print(buffer[6]);
        Serial.print(" - Distance to target: ");
        Serial.print((buffer[8] << 8) | buffer[7]);
        Serial.print("cm - Stationary target energy value: ");
        Serial.print(buffer[9]);
        Serial.print(" - Detection distance: ");
        Serial.print(buffer[10]);
        Serial.println("cm");

        stato = CHECK_HEADER;
        break;
    }
  }
}
