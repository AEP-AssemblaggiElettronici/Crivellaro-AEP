/*
  ============================================================================
  ESP32-S3 SENSOR NODE - Sistema di Acquisizione Dati Multi-Sensore
  ============================================================================
  
  VERSIONE: 2.0.0
  DATA: Gennaio 2025
  AUTORE: Fabio Crivellaro
  
  DESCRIZIONE:
  Sistema di acquisizione dati da sensori multipli con trasmissione SigFox/LoRa
  e visualizzazione web tramite Access Point WiFi (solo primo ciclo).
  
  CHANGELOG:
  ----------
  v2.0.0 - Gennaio 2025
    + Aggiunto Access Point WiFi attivo SOLO nel primo ciclo
    + Pagina web HTML responsive per visualizzazione dati sensori
    + Download file JSON con tutti i dati acquisiti
    + Beep ogni 2 secondi durante attesa connessione (max 60 secondi)
    + Chiusura automatica AP dopo prima visualizzazione pagina
    + Beep di conferma (3 beep) quando utente si connette
    + SSID: "SENSOR_NODE" (Access Point aperto, nessuna password)
    + IP Access Point: 192.168.4.1
    + Endpoint: "/" (pagina HTML), "/data.json" (download JSON)
  
  v1.0.0 - Agosto-Settembre 2025
    - Versione originale con supporto radio SigFox/LoRa
    - Lettura sensori: OneWire (A/B), I2C SHT3x (A/B), RS485 (E/F)
    - Forchette analogiche umidità terreno (C/D)
    - Celle di carico per peso (C/D)
    - Pluviometro con interrupt su INT1/INT2
    - Gestione light sleep dinamico con sveglia timer o interrupt
    - Invio dati via radio ogni 15 minuti (SigFox) o 8 minuti (LoRa)
  
  NOTE HARDWARE:
  - Sensori OneWire vanno sulle porte A e B (NON C e D come ATSAMD21)
  - Alimentazione ESP32-S3 è 3V (NON 3.3V!)
  - RS485: verificare funzionamento forchette (rimosse 2 resistenze)
  
  ============================================================================
*/

#include "FS.h"
#include "defines.h"
#include "SPIFFS.h"
#include <HardwareSerial.h>
#include "esp_task_wdt.h"
#include "Wire.h"
#include "sht3x.h"
#include "rs485.h"
#include "rs485_estesa.h"
#include <OneWire.h>
#include "one_wire.h"
#include "esp_sleep.h"
#include "HX711.h"
#include <WiFi.h>       // Gestione Access Point WiFi
#include <WebServer.h>  // Web server per pagina HTML e JSON

File filer;
HardwareSerial radio(1);
HardwareSerial forkett(2);
OneWire sens1wireA(INT1);
OneWire sens1wireB(INT2);
HX711 bilancia;

String dispositivoID = "";
bool sigfoxLora = 0;
bool forchettaCdigitalePresente = 0;
bool forchettaDdigitalePresente = 0;
bool forchettaCdigitaleEstesaPresente = 0;
bool forchettaDdigitaleEstesaPresente = 0;
bool forchettaCpresente = 0;
bool forchettaDpresente = 0;
bool presenzaOneWireA = 0;
bool presenzaOneWireB = 0;
byte indirizzo1wireA[8];
byte tipo1wireA;
byte indirizzo1wireB[8];
byte tipo1wireB;
unsigned long int contaCicli = 0;
unsigned long taraturaC;  // taratura peso sulla porta C
unsigned long taraturaD;  // taratura peso sulla porta D
volatile unsigned long int pluvioCount = 0;
volatile unsigned long int tempoUltimoImpulso = 0;
volatile bool eraInSleepMode = 0;  // flag per bypassare la lettura sensori quando il wakeup proviene dall'interrupt
bool i2cPresenteA = 0;
bool i2cPresenteB = 0;
uint8_t rs485risultati[11] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t rs485risultati2[11] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t rs485_estesaRisultati[19] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t rs485_estesaRisultati2[19] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// prototipi funzioni
String command(String command);
void buzzer(int times);
void sendMessage(uint8_t msg[], int size);
void IRAM_ATTR pluvio_ISR();

void setup() {
  for (int i = 0; i < 7; i++) {
    pinMode(unusedPins[i], INPUT);
    digitalWrite(unusedPins[i], 0);
  }  // metto giu i pin non utilizzati per risparmiare corrente

  setCpuFrequencyMhz(80);  // Imposta frequenza CPU a 80MHz (invece di 240MHz default)

  Serial.begin(SERIAL_BAUD);
  radio.begin(RADIO_BAUD, SERIAL_8N1, RXpin, TXpin);
  delay(2000);

  pinMode(BOOST_EN, OUTPUT);
  pinMode(BOOST_SHTDWN, OUTPUT);
  pinMode(PIN_FORK_C_PRESENZA, INPUT_PULLUP);
  pinMode(PIN_FORK_D_PRESENZA, INPUT_PULLUP);
  pinMode(I2C_SELECT, OUTPUT);
  pinMode(IO_ENABLE, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(PIN_FORK_C, INPUT_PULLUP);
  pinMode(PIN_FORK_D, INPUT_PULLUP);
  pinMode(RS485_DE, OUTPUT);
  pinMode(RS485_RE, OUTPUT);
  pinMode(RS485_TX_1, OUTPUT);
  pinMode(RS485_TX_2, OUTPUT);
  pinMode(RS485_RX_1, INPUT);
  pinMode(RS485_RX_2, INPUT);
  pinMode(PIN_SDA, OUTPUT);
  pinMode(PIN_SCL, OUTPUT);
  pinMode(INT1, INPUT);
  pinMode(INT2, INPUT);
  delay(5000);

  Serial.println("Rilevamento forchette su porte C e D (o E e F)");
  if (!digitalRead(PIN_FORK_C_PRESENZA) || !digitalRead(PIN_FORK_D_PRESENZA)) {
    digitalWrite(BOOST_EN, 1);
    digitalWrite(BOOST_SHTDWN, 1);
    delay(10);
    Serial.println("Forchette presenti sulle porte indicate");
    Serial.println("Rilevamento forchette digitali su porte E ed F");
    if (!digitalRead(PIN_FORK_C_PRESENZA)) {
      Serial.println("Lettura forchetta RS485 su porta E:");
      forkett.begin(FORKETT_BAUD, SERIAL_8N1, RS485_RX_1, RS485_TX_1);
      delay(100);
      for (int j = 0; j < 10; j++) {
        rs485(forkett, rs485risultati);
        delay(100);
        rs485_estesa(forkett, rs485_estesaRisultati, comandoLetturaUniversale);
        delay(100);
        if (rs485_estesaRisultati[5] != 0xFF) {
          Serial.println("Forchetta digitale PH e conducibilità presente su porta E");
          forchettaCdigitaleEstesaPresente = 1;
          break;
        } else if (rs485risultati[0] != 0xFF) {
          Serial.println("Forchetta digitale presente su porta E");
          forchettaCdigitalePresente = 1;
          break;
        } else {
          Serial.println("Forchetta analogica su porta C presente");
          forchettaCpresente = 1;
          break;
        }
      }
    }
    if (!digitalRead(PIN_FORK_D_PRESENZA)) {
      Serial.println("Lettura forchetta RS485 su porta F:");
      forkett.begin(FORKETT_BAUD, SERIAL_8N1, RS485_RX_2, RS485_TX_2);
      delay(100);
      for (int j = 0; j < 10; j++) {
        rs485(forkett, rs485risultati2);
        delay(100);
        rs485_estesa(forkett, rs485_estesaRisultati2, comandoLetturaUniversale);
        delay(100);
        if (rs485_estesaRisultati2[5] != 0xFF) {
          Serial.println("Forchetta digitale PH e conducibilità presente su porta F");
          forchettaDdigitaleEstesaPresente = 1;
          break;
        } else if (rs485risultati2[0] != 0xFF) {
          Serial.println("Forchetta digitale presente su porta F");
          forchettaDdigitalePresente = 1;
          break;
        } else {
          Serial.println("Forchetta analogica su porta D presente");
          forchettaDpresente = 1;
          break;
        }
      }
    }
  }

  if (!forchettaCpresente && !forchettaDpresente && !forchettaCdigitalePresente && !forchettaDdigitalePresente && !forchettaCdigitaleEstesaPresente && !forchettaDdigitalePresente) {
    digitalWrite(BOOST_EN, 1);
    digitalWrite(BOOST_SHTDWN, 0);
    bilancia.power_up();
    delay(100);

    Serial.println("Taratura sensori peso su porta C:");
    bilancia.begin(PIN_BIL_C, PIN_FORK_C);
    uint32_t mediaBilancia = 0;
    delay(100);
    for (int i = 10; i--;) {
      mediaBilancia += bilancia.read();
      delay(50);
    }
    taraturaC = mediaBilancia / 10;
    Serial.println(taraturaC);
    delay(100);
    Serial.println("Taratura sensori peso su porta D:");
    bilancia.begin(PIN_BIL_D, PIN_FORK_D);
    mediaBilancia = 0;
    delay(100);
    for (int i = 10; i--;) {
      mediaBilancia += bilancia.read();
      delay(50);
    }
    taraturaD = mediaBilancia / 10;
    Serial.println(taraturaD);
    delay(100);
  }

  Serial.println("Rilevamento sensori OneWire su porte A e B...");
  for (int i = 0; i < 10; i++) {
    if (init_1wire(sens1wireA, indirizzo1wireA, tipo1wireA)) {
      presenzaOneWireA = 1;
      break;
    }
  }
  for (int i = 0; i < 10; i++) {
    if (init_1wire(sens1wireB, indirizzo1wireB, tipo1wireB)) {
      presenzaOneWireB = 1;
      break;
    }
  }

  Serial.println("Rilevamento periferiche I2C...");
  Wire.begin();
  pinMode(IO_ENABLE, OUTPUT);
  pinMode(I2C_SELECT, OUTPUT);
  digitalWrite(IO_ENABLE, 1);
  digitalWrite(I2C_SELECT, 0);
  delay(100);
  for (int i = 2; i--;) {
    if (i2c_scan_sht3x()) {
      if (digitalRead(I2C_SELECT) == 1) i2cPresenteA = 1;
      else i2cPresenteB = 1;
      if (i2cPresenteA) Serial.println("Sensore I2C rilevato su porta A");
      else if (i2cPresenteB) Serial.println("Sensore I2C rilevato su porta B");
      break;
    }
    digitalWrite(I2C_SELECT, 1);
    delay(100);
  }

  digitalWrite(IO_ENABLE, 0);
  digitalWrite(BOOST_EN, 0);
  digitalWrite(BOOST_SHTDWN, 0);
  delay(500);

  // se  lo SPIFFS non ha all'interno questo file, formattiamo lo SPIFFS
  if (!SPIFFS.begin()) {
    Serial.println("Errore nell'inizializzazione di SPIFFS!");
  }

  if (!SPIFFS.exists("/1st_boot")) {
    Serial.println("Formattiamo lo SPIFFS");
    if (SPIFFS.format()) Serial.println("Formattazione eseguita con successo.");
    SPIFFS.end();
    SPIFFS.begin();
    filer = SPIFFS.open("/1st_boot", FILE_WRITE);
    filer.close();
  }
  SPIFFS.end();

  Serial.println("Rilevamento modulo radio (SigFox o LoRa)...");
  uint8_t conteggioRisposta = 0;
  String rispostaModulo = "";
  do {
    rispostaModulo = command("AT$I=11\r");
    if (rispostaModulo.length() == 18) break;
    conteggioRisposta++;
  } while (conteggioRisposta < 4);
  delay(1000);
  if (rispostaModulo == "" || rispostaModulo.length() != 18) {  // 18 è la lunghezza del PAC in sigfox
    Serial.println("Modulo LoRa installato");
    radio.flush();
    radio.end();
    sigfoxLora = 1;
    delay(3000);

    if (!SPIFFS.begin()) {
      Serial.println("Errore nell'inizializzazione di SPIFFS!");
    }
    filer = SPIFFS.open("/dispositivo_id.txt", FILE_READ);
    if (filer) {
      while (filer.available()) {
        dispositivoID = filer.readStringUntil('\n');
      }
    }
    filer.close();

    ////////////////////////////////////////////////////////// Settaggio ID dispositivo
    Serial.print("ID dispositivo in memoria: ");
    Serial.print(dispositivoID);
    Serial.println();
    Serial.println("Cambiare ID e protocollo dispositivo? (premere entro 5 secondi 's' o premere qualsiasi altro tasto per procedere)");
    unsigned int tempoEditDispositivo = millis();
    while (!Serial.available() && millis() - tempoEditDispositivo < 5000)
      ;  // attende 5 secondi per la pressione del tasto 's'
    if (Serial.available()) {
      if (Serial.read() == 's') {
        dispositivoID = "";
        Serial.println("Inserire nuovo ID ('Lxxxxx'): ");
        int iID = 0;
        while (iID < 5) {
          while (!Serial.available())
            ;
          dispositivoID += Serial.readStringUntil('\n');
          iID += dispositivoID.length();
        }
        if (dispositivoID.length() > 6) {
          Serial.println(dispositivoID);
          Serial.println("ID dispositivo più lungo di 5 caratteri, riprovare.");
          delay(1000);
          ESP.restart();
        }

        Serial.print(dispositivoID);

        dispositivoID = 'L' + dispositivoID;
        Serial.println();
        Serial.print("ID dispositivo: ");
        Serial.println(dispositivoID);
        if (!SPIFFS.begin(true)) {
          Serial.println("Errore nell'inizializzazione di SPIFFS!");
        }

        SPIFFS.remove("/dispositivo_id.txt");
        filer = SPIFFS.open("/dispositivo_id.txt", FILE_WRITE);
        if (!filer) {
          Serial.println("Errore apertura file in scrittura!");
        } else {
          Serial.print("Scrittura ID su file...");
          filer.println(dispositivoID);
          filer.close();
        }
        Serial.println(" Scrittura ID completata.");
        SPIFFS.end();
        delay(1000);
      }
    }
  } else {
    Serial.println("Modulo SigFox installato");
    radio.flush();
    radio.end();
  }

  digitalWrite(BOOST_EN, 0);  // BOOST_EN = 1 attiva i 12v PER LE FORCHETTE, se è a 0 l'output è a 4v (per le BILANCE)
  digitalWrite(BOOST_SHTDWN, 0);

  attachInterrupt(INT1, pluvio_ISR, FALLING);
  attachInterrupt(INT2, pluvio_ISR, FALLING);

  delay(1000);
  Serial.println("DEBUG - fine setup");  // DEBUG
}

void loop() {
  delay(200);
  // se sigfox, 15 minuti, se lora, 8 (valori espressi in microsecondi)
  static uint64_t cicloDurataUs = !sigfoxLora ? 900ULL /* 20ULL */ * 1000000ULL : 480ULL /* 20ULL */ * 1000000ULL;
  static uint64_t cicloInizio = 0;
  if (!eraInSleepMode) {  // se non siamo in lightsleep e veniamo risvegliati dall'interrupt pluviometro, legge i dati sensori come dovrebbe

    if (contaCicli != 0) {
      Serial.begin(115200);
      delay(500);
      Serial.print("Era in sleep mode? ");           // DEBUG
      Serial.println(eraInSleepMode ? "si" : "no");  // DEBUG
      Serial.println(pluvioCount);                   // DEBUG
      Serial.println("Risveglio...");

      pinMode(PIN_SDA, OUTPUT);
      pinMode(PIN_SCL, OUTPUT);
      pinMode(PIN_BATTERY, INPUT);
      pinMode(BOOST_EN, OUTPUT);
      pinMode(BOOST_SHTDWN, OUTPUT);
      pinMode(IO_ENABLE, OUTPUT);
      pinMode(I2C_SELECT, OUTPUT);
      pinMode(RS485_DE, OUTPUT);
      pinMode(RS485_RE, OUTPUT);
      pinMode(RS485_RX_1, INPUT);
      pinMode(RS485_TX_1, OUTPUT);
      pinMode(RS485_RX_2, INPUT);
      pinMode(RS485_TX_2, OUTPUT);
      pinMode(BUZZER, OUTPUT);

      digitalWrite(BOOST_EN, 0);
      digitalWrite(BOOST_SHTDWN, 0);
      delay(100);
    }

    buzzer(0);

    /////////////////////////////////////
    // DA QUI RACCOGLIE I DATI DEI SENSORI
    /////////////////////////////////////

    uint16_t batteria = 0;
    uint32_t batteriaMedia = 0;
    uint16_t forchettaAnalogC = 0xFFFE;
    uint16_t forchettaAnalogD = 0xFFFE;
    unsigned long pesoPrecedente1 = 0xFFFE;
    unsigned long pesoPrecedente2 = 0xFFFE;
    long peso1 = 0xFFFE;
    long peso2 = 0xFFFE;
    long pesoGrammi1 = 0xFFFE;
    long pesoGrammi2 = 0xFFFE;
    uint16_t rs485TempE = 0xFFFE;
    uint16_t rs485HumE = 0xFFFE;
    uint16_t rs485TempF = 0xFFFE;
    uint16_t rs485HumF = 0xFFFE;
    uint16_t rs485PiaccaE = 0xFFFE;
    uint16_t rs485ConducibilitaE = 0xFFFE;
    uint16_t rs485PiaccaF = 0xFFFE;
    uint16_t rs485ConducibilitaF = 0xFFFE;
    double accumuloTempE = 0;
    double accumuloHumE = 0;
    double accumuloTempF = 0;
    double accumuloHumF = 0;
    uint8_t rs485risultati[11] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t rs485risultati2[11] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uint16_t shtTempA = 0xFF;
    uint16_t shtHumA = 0xFF;
    uint16_t shtTempB = 0xFFFE;
    uint16_t shtHumB = 0xFFFE;
    uint16_t *valori = nullptr;
    float temperatura1wireA = 0xFFFE;
    float temperatura1wireB = 0xFFFE;
    unsigned int temperatura1wireAint = 0xFFFE;
    unsigned int temperatura1wireBint = 0xFFFE;

    Serial.print("Ciclo: ");
    Serial.print(contaCicli);
    Serial.println();

    setCpuFrequencyMhz(240);

    for (int i = 10; i--;) {
      batteriaMedia += analogReadMilliVolts(PIN_BATTERY) * 2;
      delay(50);
    }
    batteria = ((batteriaMedia / 10) * 1.03) + 40;
    Serial.print("Lettura batteria (mV): ");
    Serial.print(batteria);
    Serial.println();

    if (presenzaOneWireA || presenzaOneWireB || i2cPresenteA || i2cPresenteB) {
      Wire.begin();
      pinMode(IO_ENABLE, OUTPUT);
      digitalWrite(IO_ENABLE, 1);
      delay(100);

      if (!presenzaOneWireA) {
        digitalWrite(I2C_SELECT, 1);  // scansione su porta A
        delay(500);
        if (i2cPresenteA) {
          valori = sht3x(SHT3X_ADDRESS);
          if (!sigfoxLora) {
            shtTempA = valori[0];
            shtHumA = valori[1];
          } else {
            shtTempA = valori[2];
            shtHumA = valori[3];
          }
        }
      } else {
        temperatura1wireA = read_1wire(sens1wireA, indirizzo1wireA, tipo1wireA);
        temperatura1wireAint = (int)(temperatura1wireA * 10);
      }

      if (!presenzaOneWireB) {
        digitalWrite(I2C_SELECT, 0);  // scansione su porta B
        delay(500);
        if (i2cPresenteB) {
          valori = sht3x(SHT3X_ADDRESS);
          if (!sigfoxLora) {
            shtTempB = valori[0] | valori[5];
            shtHumB = valori[1];
          } else {
            shtTempB = valori[2];
            shtHumB = valori[3];
          }
        }
      } else {
        temperatura1wireB = read_1wire(sens1wireB, indirizzo1wireB, tipo1wireB);
        temperatura1wireBint = (int)(temperatura1wireB * 10);
      }

      Wire.end();

      digitalWrite(IO_ENABLE, 0);
    }

    delay(100);

    if (forchettaCpresente || forchettaDpresente || forchettaCdigitalePresente || forchettaDdigitalePresente || forchettaCdigitaleEstesaPresente || forchettaDdigitaleEstesaPresente) {
      digitalWrite(BOOST_SHTDWN, 1);
      digitalWrite(BOOST_EN, 1);
      delay(200);
      if (forchettaCdigitalePresente || forchettaCdigitaleEstesaPresente) {
        if (forchettaCdigitaleEstesaPresente) {
          forkett.begin(FORKETT_BAUD, SERIAL_8N1, RS485_RX_1, RS485_TX_1);
          delay(10);
          uint16_t accumuloTempE = 0;
          uint16_t accumuloHumE = 0;
          uint16_t accumuloPiaccaE = 0;
          uint16_t accumuloConducibilitaE = 0;
          uint8_t rs485_estesaRisultati[19] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
          for (int j = 0; j < 2; j++) {
            rs485_estesa(forkett, rs485_estesaRisultati, comandoLetturaUniversale);
            accumuloTempE = (rs485_estesaRisultati[5] << 8) | rs485_estesaRisultati[6];
            accumuloHumE = (rs485_estesaRisultati[3] << 8) | rs485_estesaRisultati[4];
            accumuloPiaccaE = (rs485_estesaRisultati[9] << 8) | rs485_estesaRisultati[10];
            accumuloConducibilitaE = (rs485_estesaRisultati[7] << 8) | rs485_estesaRisultati[8];
            delay(3000);
          }
          rs485TempE = accumuloTempE;
          rs485HumE = accumuloHumE;
          rs485PiaccaE = accumuloPiaccaE;
          rs485ConducibilitaE = accumuloConducibilitaE;
        } else if (forchettaCdigitalePresente) {
          forkett.begin(FORKETT_BAUD, SERIAL_8N1, RS485_RX_1, RS485_TX_1);
          delay(10);
          double accumuloTempE = 0;
          double accumuloHumE = 0;
          uint8_t rs485risultati[11] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
          for (int j = 0; j < 10; j++) {
            rs485(forkett, rs485risultati);
            if (rs485risultati[0] != 0xFF) {
              accumuloTempE += ((rs485risultati[5] << 8) | rs485risultati[6]);
              accumuloHumE += ((rs485risultati[3] << 8) | rs485risultati[4]);
            }
          }
          rs485TempE = accumuloTempE / 10;
          rs485HumE = accumuloHumE / 10;
        }
      } else if (forchettaCpresente) {
        uint32_t mediaForchetta = 0;
        for (int i = 0; i < 10; i++) {
          mediaForchetta += analogRead(PIN_FORK_C);
          delay(50);
        }
        forchettaAnalogC = (mediaForchetta / 10) >> 2;
        if (forchettaAnalogC > 1000) forchettaAnalogC = 1000;
      }

      delay(300);

      if (forchettaDdigitalePresente || forchettaDdigitaleEstesaPresente) {
        if (forchettaDdigitaleEstesaPresente) {
          forkett.begin(FORKETT_BAUD, SERIAL_8N1, RS485_RX_2, RS485_TX_2);
          delay(10);
          uint16_t accumuloTempF = 0;
          uint16_t accumuloHumF = 0;
          uint16_t accumuloPiaccaF = 0;
          uint16_t accumuloConducibilitaF = 0;
          uint8_t rs485_estesaRisultati2[19] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
          for (int j = 0; j < 2; j++) {
            rs485_estesa(forkett, rs485_estesaRisultati2, comandoLetturaUniversale);
            accumuloTempF = (rs485_estesaRisultati2[5] << 8) | rs485_estesaRisultati2[6];
            accumuloHumF = (rs485_estesaRisultati2[3] << 8) | rs485_estesaRisultati2[4];
            accumuloPiaccaF = (rs485_estesaRisultati2[9] << 8) | rs485_estesaRisultati2[10];
            accumuloConducibilitaF = (rs485_estesaRisultati2[7] << 8) | rs485_estesaRisultati2[8];
            delay(3000);
          }
          rs485TempF = accumuloTempF;
          rs485HumF = accumuloHumF;
          rs485PiaccaF = accumuloPiaccaF;
          rs485ConducibilitaF = accumuloConducibilitaF;
        } else if (forchettaDdigitalePresente) {
          forkett.begin(FORKETT_BAUD, SERIAL_8N1, RS485_RX_2, RS485_TX_2);
          delay(10);
          double accumuloTempF = 0;
          double accumuloHumF = 0;
          uint8_t rs485risultati2[11] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
          for (int j = 0; j < 10; j++) {
            rs485(forkett, rs485risultati2);
            if (rs485risultati2[0] != 0xFF) {
              accumuloTempF += ((rs485risultati2[5] << 8) | rs485risultati2[6]);
              accumuloHumF += ((rs485risultati2[3] << 8) | rs485risultati2[4]);
            }
          }
          rs485TempF = accumuloTempF / 10;
          rs485HumF = accumuloHumF / 10;
        } else if (forchettaDpresente) {
          uint32_t mediaForchetta = 0;
          for (int i = 0; i < 10; i++) {
            mediaForchetta += analogRead(PIN_FORK_D);
            delay(50);
          }
          forchettaAnalogD = (mediaForchetta / 10) >> 2;
          if (forchettaAnalogD > 1000) forchettaAnalogD = 1000;
        }
      }
    } else {
      digitalWrite(BOOST_EN, 1);
      digitalWrite(BOOST_SHTDWN, 0);
      delay(10);
      bilancia.begin(PIN_BIL_C, PIN_FORK_C);
      delay(100);

      uint32_t mediaBilancia = 0;
      for (int i = 10; i--;) {
        mediaBilancia += bilancia.read();
      }
      peso1 = mediaBilancia - taraturaC;

      bilancia.begin(PIN_BIL_D, PIN_FORK_D);
      delay(100);

      mediaBilancia = 0;
      for (int i = 10; i--;) {
        mediaBilancia += bilancia.read();
      }
      peso2 = mediaBilancia - taraturaD;
    }
    delay(1000);

    digitalWrite(RS485_RE, 1);
    digitalWrite(RS485_DE, 0);

    digitalWrite(BOOST_EN, 0);
    digitalWrite(BOOST_SHTDWN, 0);

    setCpuFrequencyMhz(80);

    /////////////////////////////////////
    // DA QUI INVIA I DATI VIA RADIO:
    /////////////////////////////////////
    radio.flush();
    radio.end();
    delay(300);
    radio.begin(!sigfoxLora ? RADIO_BAUD : LORA_BAUD, SERIAL_8N1, RXpin, TXpin);  // a seconda del modulo usato, c'è un baudrate diverso
    delay(100);
    uint16_t randomDelay = random(1, 6000);
    if (!sigfoxLora) {
      uint8_t msgS[12];
      //-------------- MESSAGGIO SIGFOX (MAX 12 BYTE)---------
      msgS[0] = 0xA1;
      msgS[1] = 0;
      msgS[2] = forchettaAnalogC != 0xFFFE ? highByte(forchettaAnalogC) : highByte(rs485HumE);
      msgS[3] = forchettaAnalogC != 0xFFFE ? lowByte(forchettaAnalogC) : lowByte(rs485HumE);
      msgS[4] = forchettaAnalogD != 0xFFFE ? highByte(forchettaAnalogD) : highByte(rs485HumF);
      msgS[5] = forchettaAnalogD != 0xFFFE ? lowByte(forchettaAnalogD) : lowByte(rs485HumF);
      msgS[6] = shtTempA ? shtTempA : shtTempB;
      msgS[7] = shtHumA ? shtHumA : shtHumB;  // Umidità aria relativa (da versione 0) Sensore SHT
      msgS[8] = 0;
      msgS[9] = 0;
      msgS[10] = batteria;
      msgS[11] = 0xED;

      Serial.println("Messaggio SigFox: ");
      for (int i = 0; i < 12; i++) {
        Serial.print(msgS[i], HEX);
        Serial.print("|");
      }
      Serial.println();
      delay(500);

      Serial.println("ID dispositivo SigFox:");
      Serial.println(command("AT$I=10\r"));  // otteniamo l'id
      delay(100);
      Serial.println("PAC dispositivo SigFox:");
      Serial.println(command("AT$I=11\r"));  // otteniamo il pac number
      delay(randomDelay);
      sendMessage(msgS, 12);
    } else {
      uint8_t msgL[70];
      uint8_t numeroCasuale = (uint8_t)random(1, 255);
      //-------------- MESSAGGIO LORA (MAX 70 BYTE)---------
      msgL[0] = dispositivoID[0];
      msgL[1] = dispositivoID[1];
      msgL[2] = dispositivoID[2];
      msgL[3] = dispositivoID[3];
      msgL[4] = dispositivoID[4];
      msgL[5] = dispositivoID[5];

      //-----------------------------SEGNALI DEL CONNETTORE A----------------------------------------------
      msgL[6] = highByte(0xFF);  // SEGNALE A1 tensiometro 60
      msgL[7] = lowByte(0xFE);

      msgL[8] = highByte(0xFF);  // SEGNALE A2  lux
      msgL[9] = lowByte(0xFE);

      msgL[10] = highByte(temperatura1wireAint);  // A3 SEGNALE futuro
      msgL[11] = lowByte(temperatura1wireAint);

      msgL[12] = highByte(rs485PiaccaE);  //highByte(0xFF);  //  A4 SEGNALE futuro
      msgL[13] = lowByte(rs485PiaccaE);   //lowByte(0xFE);

      msgL[14] = highByte(rs485ConducibilitaE);  //highByte(0xFF);  // A5 SEGNALE futuro
      msgL[15] = lowByte(rs485ConducibilitaE);   //lowByte(0xFE);

      msgL[16] = highByte(rs485PiaccaF);  //highByte(0xFF);  // A6 SEGNALE futuro
      msgL[17] = lowByte(rs485PiaccaF);   //lowByte(0xFE);

      //-----------------------------SEGNALI DEL CONNETTORE B----------------------------------------------
      msgL[18] = highByte(0xFF);  // SEGNALE B7  tensiometro 60
      msgL[19] = lowByte(0xFE);

      msgL[20] = highByte(0xFF);  // SEGNALE B8 Lux
      msgL[21] = lowByte(0xFE);

      msgL[22] = highByte(temperatura1wireBint);  // SEGNALE B9 Temperatura
      msgL[23] = lowByte(temperatura1wireBint);

      msgL[24] = highByte(pluvioCount);  // SEGNALE B10 Pluviometro
      msgL[25] = lowByte(pluvioCount);

      msgL[26] = highByte(pluvioCount);  // SEGNALE B11 Drenato
      msgL[27] = lowByte(pluvioCount);

      msgL[28] = highByte(rs485ConducibilitaF);  //highByte(0xFF);  // B12 SEGNALE futuro
      msgL[29] = lowByte(rs485ConducibilitaF);   //lowByte(0xFE);

      //-----------------------------SEGNALI DEL CONNETTORE C----------------------------------------------
      msgL[30] = highByte(0xFF);  // SEGNALE C13 Temperatura
      msgL[31] = lowByte(0xFE);

      msgL[32] = highByte(0xFF);  // SEGNALE C14 anemometro
      msgL[33] = lowByte(0xFE);

      msgL[34] = highByte(forchettaAnalogC);  // C15 forchetta analogica umidità
      msgL[35] = lowByte(forchettaAnalogC);

      msgL[36] = highByte(0xFF);  // SEGNALE 16  analog ( PAR, Soil Moist, Press, PH, Leaf WET)
      msgL[37] = lowByte(0xFE);

      msgL[38] = highByte(shtTempA);  // SEGNALE C17  TEMP_C
      msgL[39] = lowByte(shtTempA);

      msgL[40] = highByte(shtHumA);  // SEGNALE C18  UR_C
      msgL[41] = lowByte(shtHumA);

      msgL[42] = highByte(0xFF);  // SEGNALE C19  EC
      msgL[43] = lowByte(0xFE);

      msgL[44] = highByte(pesoGrammi1);  // C20 SEGNALE PESO
      msgL[45] = lowByte(pesoGrammi1);

      msgL[46] = highByte(rs485TempE);  // C21 rs485 temperatura
      msgL[47] = lowByte(rs485TempE);

      msgL[48] = highByte(rs485HumE);  // C22 rs485 umidità
      msgL[49] = lowByte(rs485HumE);

      //-----------------------------SEGNALI DEL CONNETTORE D----------------------------------------------
      msgL[50] = highByte(shtTempB);  // SEGNALE D23 TEMP_D
      msgL[51] = lowByte(shtTempB);

      msgL[52] = highByte(shtHumB);  // SEGNALE D24 UR_D
      msgL[53] = lowByte(shtHumB);

      msgL[54] = highByte(0xFF);  // SEGNALE 25   ( PAR, Soil Moist, Press, PH, Leaf WET)
      msgL[55] = lowByte(0xFE);

      msgL[56] = highByte(pesoGrammi2);  // SEGNALE D26  PESO
      msgL[57] = lowByte(pesoGrammi2);

      msgL[58] = highByte(forchettaAnalogD);  // SEGNALE D27 forchetta analogica umidità terreno
      msgL[59] = lowByte(forchettaAnalogD);

      msgL[60] = highByte(rs485TempF);  // D28 rs485 temperatura
      msgL[61] = lowByte(rs485TempF);

      msgL[62] = highByte(rs485HumF);  // D29 rs485 umidità
      msgL[63] = lowByte(rs485HumF);

      //----------------------------- TRASMISSIONE ----------------------------------------------
      msgL[64] = highByte(batteria);  // SEGNALE BAT30
      msgL[65] = lowByte(batteria);

      msgL[66] = numeroCasuale;  // CODICE RANDOM TRASMISSIONE
      msgL[67] = 0;              // invio caratteri di fine messaggio
      msgL[68] = 0xFF;
      msgL[69] = 0xFF;

      pinMode(PIN_SIGFOX_RESET, OUTPUT);  // sveglia il modulo LoRa
      digitalWrite(PIN_SIGFOX_RESET, 0);
      delay(100);

      Serial.println("Messaggio:");
      for (int i = 0; i < 70; i++) {
        Serial.print(msgL[i], HEX);
        Serial.print("|");
      }
      Serial.println();
      delay(randomDelay);

      Serial.println("Invio messaggio LoRa...");
      radio.write(msgL, 70);
    }

    /////////////////////////////////////
    // BLOCCO ACCESS POINT WiFi (SOLO PRIMO CICLO)
    /////////////////////////////////////
    if (contaCicli == 0) {
      Serial.println("\n========================================");
      Serial.println("    ACCESS POINT WiFi ATTIVO");
      Serial.println("========================================");

      // Configurazione WiFi Access Point
      WiFi.mode(WIFI_AP);
      WiFi.softAP("SENSOR_NODE");  // SSID fisso, nessuna password
      delay(100);

      Serial.print("SSID: SENSOR_NODE\n");
      Serial.print("IP Address: ");
      Serial.println(WiFi.softAPIP());  // Tipicamente 192.168.4.1
      Serial.println("Password: nessuna (AP aperto)");
      Serial.println("\nAccedere da browser a: http://192.168.4.1");
      Serial.println("Timeout: 60 secondi");
      Serial.println("Beep ogni 2 secondi durante attesa...\n");

      // Setup Web Server
      WebServer server(80);
      bool paginaVisualizzata = false;

      // ========== HANDLER PAGINA HTML PRINCIPALE ==========
      server.on("/", [&]() {
        String html = "<!DOCTYPE html><html><head>";
        html += "<meta charset='UTF-8'>";
        html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
        html += "<title>Sensor Node - Dati</title>";
        html += "<style>";
        html += "body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f5f5f5}";
        html += ".container{max-width:800px;margin:0 auto;background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}";
        html += "h1{color:#2c3e50;margin:0 0 20px 0;font-size:24px;border-bottom:3px solid #3498db;padding-bottom:10px}";
        html += ".info{background:#e8f4f8;padding:15px;border-radius:5px;margin-bottom:20px;border-left:4px solid #3498db}";
        html += ".btn{background:#3498db;color:white;padding:12px 24px;border:none;border-radius:5px;cursor:pointer;font-size:16px;text-decoration:none;display:inline-block;margin:10px 0}";
        html += ".btn:hover{background:#2980b9}";
        html += "table{width:100%;border-collapse:collapse;margin:20px 0}";
        html += "th{background:#34495e;color:white;padding:12px;text-align:left;font-weight:600}";
        html += "td{padding:10px 12px;border-bottom:1px solid #ddd}";
        html += "tr:hover{background:#f8f9fa}";
        html += ".section-header{background:#3498db;color:white;font-weight:600}";
        html += ".nd{color:#999;font-style:italic}";
        html += ".value{font-weight:600;color:#2c3e50}";
        html += ".footer{text-align:center;color:#7f8c8d;font-size:12px;margin-top:30px;padding-top:20px;border-top:1px solid #ddd}";
        html += "@media(max-width:600px){body{padding:10px}.container{padding:15px}h1{font-size:20px}td,th{padding:8px;font-size:14px}}";
        html += "</style></head><body>";

        html += "<div class='container'>";
        html += "<h1>📡 SENSOR NODE - Letture Sensori</h1>";

        html += "<div class='info'>";
        html += "<strong>ℹ️ Info:</strong> Questo Access Point si chiuderà automaticamente dopo questa visualizzazione.<br>";
        html += "Per scaricare i dati in formato JSON clicca il pulsante qui sotto.";
        html += "</div>";

        html += "<a href='/data.json' class='btn'>📥 Scarica JSON</a>";

        html += "<table>";

        // ===== SEZIONE INFORMAZIONI GENERALI =====
        html += "<tr><th colspan='2' class='section-header'>📊 Informazioni Generali</th></tr>";
        html += "<tr><td>Tipo Radio</td><td class='value'>" + String(sigfoxLora ? "LoRa" : "SigFox") + "</td></tr>";
        if (sigfoxLora) {
          html += "<tr><td>ID Dispositivo</td><td class='value'>" + dispositivoID + "</td></tr>";
        }
        html += "<tr><td>Numero Ciclo</td><td class='value'>" + String(contaCicli) + "</td></tr>";
        html += "<tr><td>Batteria</td><td class='value'>" + String(batteria) + " mV</td></tr>";

        // ===== PORTA A =====
        html += "<tr><th colspan='2' class='section-header'>🔌 Porta A</th></tr>";
        if (presenzaOneWireA) {
          html += "<tr><td>Tipo Sensore</td><td class='value'>OneWire</td></tr>";
          if (temperatura1wireAint != 0xFFFE) {
            html += "<tr><td>Temperatura</td><td class='value'>" + String(temperatura1wireA, 1) + " °C</td></tr>";
          } else {
            html += "<tr><td>Temperatura</td><td class='nd'>N/D</td></tr>";
          }
        } else if (i2cPresenteA) {
          html += "<tr><td>Tipo Sensore</td><td class='value'>I2C (SHT3x)</td></tr>";
          if (shtTempA != 0xFF && shtTempA != 0xFFFE) {
            html += "<tr><td>Temperatura</td><td class='value'>" + String(shtTempA / 10.0, 1) + " °C</td></tr>";
            html += "<tr><td>Umidità</td><td class='value'>" + String(shtHumA) + " %</td></tr>";
          } else {
            html += "<tr><td colspan='2' class='nd'>Dati non disponibili</td></tr>";
          }
        } else {
          html += "<tr><td colspan='2' class='nd'>Nessun sensore rilevato</td></tr>";
        }

        // ===== PORTA B =====
        html += "<tr><th colspan='2' class='section-header'>🔌 Porta B</th></tr>";
        if (presenzaOneWireB) {
          html += "<tr><td>Tipo Sensore</td><td class='value'>OneWire</td></tr>";
          if (temperatura1wireBint != 0xFFFE) {
            html += "<tr><td>Temperatura</td><td class='value'>" + String(temperatura1wireB, 1) + " °C</td></tr>";
          } else {
            html += "<tr><td>Temperatura</td><td class='nd'>N/D</td></tr>";
          }
        } else if (i2cPresenteB) {
          html += "<tr><td>Tipo Sensore</td><td class='value'>I2C (SHT3x)</td></tr>";
          if (shtTempB != 0xFFFE) {
            html += "<tr><td>Temperatura</td><td class='value'>" + String(shtTempB / 10.0, 1) + " °C</td></tr>";
            html += "<tr><td>Umidità</td><td class='value'>" + String(shtHumB) + " %</td></tr>";
          } else {
            html += "<tr><td colspan='2' class='nd'>Dati non disponibili</td></tr>";
          }
        } else {
          html += "<tr><td colspan='2' class='nd'>Nessun sensore rilevato</td></tr>";
        }
        html += "<tr><td>Pluviometro (impulsi)</td><td class='value'>" + String(pluvioCount) + "</td></tr>";

        // ===== PORTA C (E) =====
        html += "<tr><th colspan='2' class='section-header'>🔌 Porta C (E)</th></tr>";
        if (forchettaCdigitalePresente) {
          html += "<tr><td>Tipo Sensore</td><td class='value'>Forchetta RS485</td></tr>";
          if (rs485TempE != 0xFFFE) {
            html += "<tr><td>Temperatura</td><td class='value'>" + String(rs485TempE / 10.0, 1) + " °C</td></tr>";
            html += "<tr><td>Umidità Terreno</td><td class='value'>" + String(rs485HumE / 10.0, 1) + " %</td></tr>";
          } else {
            html += "<tr><td colspan='2' class='nd'>Dati non disponibili</td></tr>";
          }
        } else if (forchettaCpresente) {
          html += "<tr><td>Tipo Sensore</td><td class='value'>Forchetta Analogica</td></tr>";
          if (forchettaAnalogC != 0xFFFE) {
            html += "<tr><td>Valore Umidità</td><td class='value'>" + String(forchettaAnalogC) + " / 1000</td></tr>";
          } else {
            html += "<tr><td>Valore</td><td class='nd'>N/D</td></tr>";
          }
        } else if (peso1 != 0xFFFE) {
          html += "<tr><td>Tipo Sensore</td><td class='value'>Cella di Carico</td></tr>";
          html += "<tr><td>Peso (raw)</td><td class='value'>" + String(peso1) + "</td></tr>";
          if (pesoGrammi1 != 0xFFFE) {
            html += "<tr><td>Peso</td><td class='value'>" + String(pesoGrammi1) + " g</td></tr>";
          }
        } else {
          html += "<tr><td colspan='2' class='nd'>Nessun sensore rilevato</td></tr>";
        }

        // ===== PORTA D (F) =====
        html += "<tr><th colspan='2' class='section-header'>🔌 Porta D (F)</th></tr>";
        if (forchettaDdigitalePresente) {
          html += "<tr><td>Tipo Sensore</td><td class='value'>Forchetta RS485</td></tr>";
          if (rs485TempF != 0xFFFE) {
            html += "<tr><td>Temperatura</td><td class='value'>" + String(rs485TempF / 10.0, 1) + " °C</td></tr>";
            html += "<tr><td>Umidità Terreno</td><td class='value'>" + String(rs485HumF / 10.0, 1) + " %</td></tr>";
          } else {
            html += "<tr><td colspan='2' class='nd'>Dati non disponibili</td></tr>";
          }
        } else if (forchettaDpresente) {
          html += "<tr><td>Tipo Sensore</td><td class='value'>Forchetta Analogica</td></tr>";
          if (forchettaAnalogD != 0xFFFE) {
            html += "<tr><td>Valore Umidità</td><td class='value'>" + String(forchettaAnalogD) + " / 1000</td></tr>";
          } else {
            html += "<tr><td>Valore</td><td class='nd'>N/D</td></tr>";
          }
        } else if (peso2 != 0xFFFE) {
          html += "<tr><td>Tipo Sensore</td><td class='value'>Cella di Carico</td></tr>";
          html += "<tr><td>Peso (raw)</td><td class='value'>" + String(peso2) + "</td></tr>";
          if (pesoGrammi2 != 0xFFFE) {
            html += "<tr><td>Peso</td><td class='value'>" + String(pesoGrammi2) + " g</td></tr>";
          }
        } else {
          html += "<tr><td colspan='2' class='nd'>Nessun sensore rilevato</td></tr>";
        }

        html += "</table>";

        html += "<div class='footer'>";
        html += "ESP32-S3 Sensor Node v2.0.0<br>";
        html += "Connessione chiusa dopo questa visualizzazione";
        html += "</div>";

        html += "</div></body></html>";

        server.send(200, "text/html", html);
        paginaVisualizzata = true;
        Serial.println("✓ Pagina HTML servita - chiusura AP imminente");
      });

      // ========== HANDLER JSON ==========
      server.on("/data.json", [&]() {
        String json = "{\n";
        json += "  \"device_info\": {\n";
        json += "    \"radio_type\": \"" + String(sigfoxLora ? "LoRa" : "SigFox") + "\",\n";
        if (sigfoxLora) {
          json += "    \"device_id\": \"" + dispositivoID + "\",\n";
        }
        json += "    \"cycle_number\": " + String(contaCicli) + ",\n";
        json += "    \"battery_mv\": " + String(batteria) + ",\n";
        json += "    \"timestamp_ms\": " + String(millis()) + "\n";
        json += "  },\n";

        // PORTA A
        json += "  \"port_a\": {\n";
        if (presenzaOneWireA) {
          json += "    \"sensor_type\": \"onewire\",\n";
          if (temperatura1wireAint != 0xFFFE) {
            json += "    \"temperature_c\": " + String(temperatura1wireA, 2) + "\n";
          } else {
            json += "    \"temperature_c\": null\n";
          }
        } else if (i2cPresenteA) {
          json += "    \"sensor_type\": \"i2c_sht3x\",\n";
          if (shtTempA != 0xFF && shtTempA != 0xFFFE) {
            json += "    \"temperature_c\": " + String(shtTempA / 10.0, 1) + ",\n";
            json += "    \"humidity_pct\": " + String(shtHumA) + "\n";
          } else {
            json += "    \"temperature_c\": null,\n";
            json += "    \"humidity_pct\": null\n";
          }
        } else {
          json += "    \"sensor_type\": \"none\"\n";
        }
        json += "  },\n";

        // PORTA B
        json += "  \"port_b\": {\n";
        if (presenzaOneWireB) {
          json += "    \"sensor_type\": \"onewire\",\n";
          if (temperatura1wireBint != 0xFFFE) {
            json += "    \"temperature_c\": " + String(temperatura1wireB, 2) + ",\n";
          } else {
            json += "    \"temperature_c\": null,\n";
          }
        } else if (i2cPresenteB) {
          json += "    \"sensor_type\": \"i2c_sht3x\",\n";
          if (shtTempB != 0xFFFE) {
            json += "    \"temperature_c\": " + String(shtTempB / 10.0, 1) + ",\n";
            json += "    \"humidity_pct\": " + String(shtHumB) + ",\n";
          } else {
            json += "    \"temperature_c\": null,\n";
            json += "    \"humidity_pct\": null,\n";
          }
        } else {
          json += "    \"sensor_type\": \"none\",\n";
        }
        json += "    \"rain_count\": " + String(pluvioCount) + "\n";
        json += "  },\n";

        // PORTA C
        json += "  \"port_c_e\": {\n";
        if (forchettaCdigitalePresente) {
          json += "    \"sensor_type\": \"rs485_fork\",\n";
          if (rs485TempE != 0xFFFE) {
            json += "    \"temperature_c\": " + String(rs485TempE / 10.0, 1) + ",\n";
            json += "    \"soil_humidity_pct\": " + String(rs485HumE / 10.0, 1) + "\n";
          } else {
            json += "    \"temperature_c\": null,\n";
            json += "    \"soil_humidity_pct\": null\n";
          }
        } else if (forchettaCpresente) {
          json += "    \"sensor_type\": \"analog_fork\",\n";
          if (forchettaAnalogC != 0xFFFE) {
            json += "    \"value\": " + String(forchettaAnalogC) + "\n";
          } else {
            json += "    \"value\": null\n";
          }
        } else if (peso1 != 0xFFFE) {
          json += "    \"sensor_type\": \"load_cell\",\n";
          json += "    \"weight_raw\": " + String(peso1) + ",\n";
          if (pesoGrammi1 != 0xFFFE) {
            json += "    \"weight_g\": " + String(pesoGrammi1) + "\n";
          } else {
            json += "    \"weight_g\": null\n";
          }
        } else {
          json += "    \"sensor_type\": \"none\"\n";
        }
        json += "  },\n";

        // PORTA D
        json += "  \"port_d_f\": {\n";
        if (forchettaDdigitalePresente) {
          json += "    \"sensor_type\": \"rs485_fork\",\n";
          if (rs485TempF != 0xFFFE) {
            json += "    \"temperature_c\": " + String(rs485TempF / 10.0, 1) + ",\n";
            json += "    \"soil_humidity_pct\": " + String(rs485HumF / 10.0, 1) + "\n";
          } else {
            json += "    \"temperature_c\": null,\n";
            json += "    \"soil_humidity_pct\": null\n";
          }
        } else if (forchettaDpresente) {
          json += "    \"sensor_type\": \"analog_fork\",\n";
          if (forchettaAnalogD != 0xFFFE) {
            json += "    \"value\": " + String(forchettaAnalogD) + "\n";
          } else {
            json += "    \"value\": null\n";
          }
        } else if (peso2 != 0xFFFE) {
          json += "    \"sensor_type\": \"load_cell\",\n";
          json += "    \"weight_raw\": " + String(peso2) + ",\n";
          if (pesoGrammi2 != 0xFFFE) {
            json += "    \"weight_g\": " + String(pesoGrammi2) + "\n";
          } else {
            json += "    \"weight_g\": null\n";
          }
        } else {
          json += "    \"sensor_type\": \"none\"\n";
        }
        json += "  }\n";
        json += "}";

        server.sendHeader("Content-Disposition", "attachment; filename=sensor_data.json");
        server.send(200, "application/json", json);
        Serial.println("✓ File JSON scaricato");
      });

      // Avvio server
      server.begin();
      Serial.println("✓ Web server avviato e in ascolto sulla porta 80\n");

      // ========== LOOP DI ATTESA CON BEEP ==========
      unsigned long apStartTime = millis();
      unsigned long lastBeep = 0;

      while (millis() - apStartTime < 60000 && !paginaVisualizzata) {
        server.handleClient();

        // Beep ogni 2 secondi
        if (millis() - lastBeep >= 2000) {
          buzzer(1);
          lastBeep = millis();
        }

        delay(10);
      }

      // ========== CHIUSURA ACCESS POINT ==========
      if (paginaVisualizzata) {
        Serial.println("\n✓ Pagina visualizzata dall'utente");
        buzzer(3);  // 3 beep di conferma
      } else {
        Serial.println("\n⚠ Timeout 60 secondi raggiunto - nessuna connessione");
        buzzer(1);
      }

      server.stop();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_OFF);
      delay(500);

      Serial.println("✓ Access Point chiuso");
      Serial.println("========================================\n");
    }
    /////////////////////////////////////
    // FINE BLOCCO ACCESS POINT WiFi
    /////////////////////////////////////

    buzzer(2);
    pluvioCount = 0;
    contaCicli++;
    Serial.println("Entrata in modalità riposo..");
    forkett.end();
    bilancia.power_down();
    Wire.end();
    radio.flush();
    delay(100);
    radio.end();
    Serial.end();
    if (sigfoxLora) { pinMode(PIN_SIGFOX_RESET, INPUT); }  // mette il modulo LoRa in sleep, e consuma pochissimo
    pinMode(RS485_DE, INPUT);
    pinMode(RS485_RE, INPUT);
    digitalWrite(PIN_SDA, 0);
    pinMode(PIN_SDA, INPUT);
    digitalWrite(PIN_SCL, 0);
    pinMode(PIN_SCL, INPUT);
    pinMode(RS485_RX_1, INPUT);
    digitalWrite(RS485_RX_1, 0);
    pinMode(RS485_TX_1, INPUT);
    pinMode(RS485_RX_2, INPUT);
    digitalWrite(RS485_TX_2, 0);
    pinMode(RS485_RX_2, INPUT);
    digitalWrite(PIN_FORK_C, 0);
    pinMode(PIN_FORK_C, INPUT);
    digitalWrite(PIN_FORK_D, 0);
    pinMode(PIN_FORK_D, INPUT);
    digitalWrite(PIN_BATTERY, 0);
    pinMode(PIN_BATTERY, INPUT);
    digitalWrite(BOOST_EN, 0);
    pinMode(BOOST_EN, INPUT);
    digitalWrite(BOOST_SHTDWN, 0);
    pinMode(BOOST_SHTDWN, INPUT);
    digitalWrite(IO_ENABLE, 0);
    pinMode(IO_ENABLE, INPUT);
    digitalWrite(I2C_SELECT, 0);
    pinMode(I2C_SELECT, INPUT);
    digitalWrite(BUZZER, 0);
    pinMode(BUZZER, INPUT);
  }

  // gestione dinamica lightsleep, esegue da qui se la flag eraInSleepMode è vera:
  eraInSleepMode = 0;
  uint64_t questoIstante = esp_timer_get_time();
  if (cicloInizio == 0) cicloInizio = questoIstante;
  uint64_t tempoPassato = questoIstante - cicloInizio;
  if (tempoPassato >= cicloDurataUs) {
    cicloInizio = questoIstante;
    tempoPassato = 0;
  }
  uint64_t tempoRimanente = cicloDurataUs - tempoPassato;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  esp_sleep_enable_timer_wakeup(tempoRimanente);       // setta il tempo rimanente di sleep dopo i calcoli delle righe qui su
  uint64_t pinMask = (1ULL << INT1) | (1ULL << INT2);  // per utilizzare entrambi i pin nell'abilitazione degli interrupt di sveglia
  esp_sleep_enable_ext1_wakeup((gpio_num_t)pinMask, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_light_sleep_start();
}

String command(String command) {
  String result = "";
  char output;

  radio.print(command);
  //while (!radio.available()) delay(1);
  delay(100);

  while (radio.available()) {
    output = radio.read();
    result += output;
    delay(10);
  }

  if (1) Serial.println(result);

  return result;
}

void buzzer(int times) {
  for (int i = 0; i <= times; i++) {
    delay(100);
    for (int i = 0; i <= 500; i++) {
      digitalWrite(BUZZER, 1);
      delayMicroseconds(200);
      digitalWrite(BUZZER, 0);
      delayMicroseconds(200);
    }
  }
}

void sendMessage(uint8_t msg[], int size) {
  Serial.println("Inside sendMessage");

  String status = "";
  String hexChar = "";
  String sigfoxCommand = "";
  char output;

  sigfoxCommand += "AT$SF=";

  pinMode(PIN_SIGFOX_RESET, OUTPUT);
  delay(50);
  digitalWrite(PIN_SIGFOX_RESET, 1);
  delay(100);
  for (int i = 0; i < size; i++) {
    hexChar = String(msg[i], HEX);

    // padding
    if (hexChar.length() == 1) {
      hexChar = "0" + hexChar;
    }

    sigfoxCommand += hexChar;
  }

  Serial.println("Sending...");
  Serial.println(sigfoxCommand);
  radio.println(sigfoxCommand);

  uint8_t contateur = 0;
  while (!radio.available()) {
    Serial.println("Waiting for response");
    contateur++;
    delay(1000);
    if (contateur > 30) {
      Serial.println("Device SigFox non pronto");
      return;
    }
  }

  while (radio.available()) {
    output = (char)radio.read();
    status += output;
    delay(10);
  }
  digitalWrite(PIN_SIGFOX_RESET, 0);

  Serial.println();
  Serial.print("Status \t");
  Serial.println(status);
}

void IRAM_ATTR pluvio_ISR()  // callback interrupt pluviometro
{
  eraInSleepMode = 1;
  unsigned long int ora = millis();
  if (ora - tempoUltimoImpulso > 100) {
    pluvioCount++;
    tempoUltimoImpulso = ora;
  }
}