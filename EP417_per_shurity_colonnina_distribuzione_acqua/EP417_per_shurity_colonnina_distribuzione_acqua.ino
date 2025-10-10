#include <SoftwareSerial.h>

#define RXpin 10
#define TXpin 9
#define CONTROLpin 7  // pin di controllo ricezione: 0 trasmissione: 1, in questo caso rimarrà sempre a 0

#define LEDBUZZ 8

#define V1 2
#define V2 3
#define V3 4
#define PIN_ALLARME 6

SoftwareSerial serialeFisica(RXpin, TXpin);
enum EnumStati { ATTESA_START,
                 LETTURA_COMANDO,
                 LETTURA_DATO,
                 LETTURA_CHECKSUM,
                 ATTESA_CHIUSURA,
};
EnumStati stati;
uint8_t stato = ATTESA_START;
uint8_t dati[5];  // [HEADER] [COMMAND] [DATA] [CHECKSUM] [FOOTER]
bool alarm = 0;
uint8_t contaAllarme = 0;
unsigned long int tempoPrecedente = 0;
unsigned long int tempoAttuale;

void svuota_array_dati() {
  for (int i = 0; i < 5; i++)
    dati[i] = 0;
}

void beep(int time) {
  for (time; time--;) {
    digitalWrite(LEDBUZZ, 1);
    delayMicroseconds(100);
    digitalWrite(LEDBUZZ, 0);
    delayMicroseconds(100);
  }
}


void setup() {
  Serial.begin(115200);
  serialeFisica.begin(9600);

  pinMode(PIN_ALLARME, INPUT);
  pinMode(LEDBUZZ, OUTPUT);
  pinMode(CONTROLpin, OUTPUT);
  digitalWrite(CONTROLpin, 0);  // così abilitiamo la ricezione seriale
  pinMode(V1, OUTPUT);
  pinMode(V2, OUTPUT);
  pinMode(V3, OUTPUT);
  digitalWrite(V1, 0);
  digitalWrite(V2, 0);
  digitalWrite(V3, 0);

  delay(1000);
}

void loop() {
  // check allarme ogni secondo, se è in allarme per 10 secondi allora scatta e si blocca tutto il dispositivo
  tempoAttuale = millis();
  if (!alarm) {
    if (tempoAttuale - tempoPrecedente >= 100) {
      if (!digitalRead(PIN_ALLARME)) contaAllarme++;
      tempoPrecedente = tempoAttuale;
    }

    if (contaAllarme > 10) alarm = 1;

    if (serialeFisica.available() >= 5) {  // acchiappiamo i dati e li mettiamo in un pacchetto
      for (int i = 0; i < 5; i++) {
        dati[i] = serialeFisica.read();
        /* Serial.print(dati[i], HEX);
      Serial.print(" "); */
      }
      Serial.println();

      digitalWrite(LEDBUZZ, 1);
      delay(100);
      digitalWrite(LEDBUZZ, 0);

      if (dati[0] == 0xFF && dati[4] == 0xFE) {  // controllo bit di start e di stop
        uint8_t checksum = (dati[1] ^ dati[2]) & 0xFF;

        if (checksum == dati[3]) {  // controllo checksum
          //Serial.println("Pacchetto valido!");

          if (dati[1] == 0xAA) {  // START
            // Serial.println("Comando START");
            if (dati[2] == 0x01) {
              digitalWrite(V1, HIGH);
              //Serial.println("V1 ON");
            }
            if (dati[2] == 0x02) {
              digitalWrite(V2, HIGH);
              //Serial.println("V2 ON");
            }
            if (dati[2] == 0x03) {
              digitalWrite(V3, HIGH);
              //Serial.println("V3 ON");
            }
          } else if (dati[1] == 0xBB) {  // STOP
            // Serial.println("Comando STOP");
            digitalWrite(V1, LOW);
            digitalWrite(V2, LOW);
            digitalWrite(V3, LOW);
            //Serial.println("Tutte le valvole OFF");
          }
        } else {
          //Serial.println("Checksum errato!");
        }
      } else {
        //Serial.println("Header/Footer errati!");
      }

      svuota_array_dati();
    }
    delay(5);  // stabilizzatore loop
  } else {     // ALLARME ALLAGAMENTO - rimane attivo finchè non si spegne la macchina
    digitalWrite(V1, 0);
    digitalWrite(V2, 0);
    digitalWrite(V3, 0);
    if (millis() % 500 < 250) {
      beep(50);
    }
  }
}