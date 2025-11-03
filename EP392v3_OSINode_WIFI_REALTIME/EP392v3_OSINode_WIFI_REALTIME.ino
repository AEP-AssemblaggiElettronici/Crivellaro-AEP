/*
  ============================================================================
  ESP32-S3 SENSOR NODE - Sistema di Acquisizione Dati Multi-Sensore
  ============================================================================
  
  VERSIONE: 2.1.0
  DATA: Gennaio 2025
  AUTORE: Fabio Crivellaro
  
  DESCRIZIONE:
  Sistema di acquisizione dati da sensori multipli con trasmissione SigFox/LoRa
  e visualizzazione web real-time tramite Access Point WiFi (solo primo ciclo).
  
  CHANGELOG:
  ----------
  v2.1.0 - Gennaio 2025
    + Connessione persistente: il WiFi rimane attivo finché l'utente visualizza la pagina
    + Aggiornamento dati in tempo reale (polling AJAX ogni 5 secondi)
    + Timeout disconnessione: 15 secondi di inattività → sleep automatico
    + Indicatore connessione visivo (pallino verde/rosso)
    + Endpoint `/current_data` per JSON leggero con dati aggiornati
    + Timestamp "ultimo aggiornamento" nella pagina
    + Lettura sensori continua durante la connessione (ogni 5 secondi)
    + Beep solo all'inizio, silenzioso durante connessione attiva
  
  v2.0.0 - Gennaio 2025
    + Aggiunto Access Point WiFi attivo SOLO nel primo ciclo
    + Pagina web HTML responsive per visualizzazione dati sensori
    + Download file JSON con tutti i dati acquisiti
    + Beep ogni 2 secondi durante attesa connessione (max 60 secondi)
    + Chiusura automatica AP dopo prima visualizzazione pagina
    + SSID: "SENSOR_NODE" (Access Point aperto, nessuna password)
    + IP Access Point: 192.168.4.1
  
  v1.0.0 - Agosto-Settembre 2025
    - Versione originale con supporto radio SigFox/LoRa
    - Lettura sensori: OneWire (A/B), I2C SHT3x (A/B), RS485 (E/F)
    - Forchette analogiche umidità terreno (C/D)
    - Celle di carico per peso (C/D)
    - Pluviometro con interrupt su INT1/INT2
    - Gestione light sleep dinamico
  
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
#include <OneWire.h>
#include "one_wire.h"
#include "esp_sleep.h"
#include "HX711.h"
#include <WiFi.h>       // Gestione Access Point WiFi
#include <WebServer.h>  // Web server per pagina HTML e JSON

// Configurazione WiFi AP e timing
#define SENSOR_READ_INTERVAL_MS 5000  // Lettura sensori ogni 5 secondi
#define CLIENT_TIMEOUT_MS 15000       // Timeout disconnessione: 15 secondi
#define AP_INITIAL_TIMEOUT_MS 60000   // Timeout iniziale se nessuno si connette: 60 secondi

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

// Variabili globali per dati sensori (accessibili da web server)
uint16_t g_batteria = 0;
uint16_t g_forchettaAnalogC = 0xFFFE;
uint16_t g_forchettaAnalogD = 0xFFFE;
long g_peso1 = 0xFFFE;
long g_peso2 = 0xFFFE;
long g_pesoGrammi1 = 0xFFFE;
long g_pesoGrammi2 = 0xFFFE;
uint16_t g_rs485TempE = 0xFFFE;
uint16_t g_rs485HumE = 0xFFFE;
uint16_t g_rs485TempF = 0xFFFE;
uint16_t g_rs485HumF = 0xFFFE;
uint16_t g_shtTempA = 0xFF;
uint16_t g_shtHumA = 0xFF;
uint16_t g_shtTempB = 0xFFFE;
uint16_t g_shtHumB = 0xFFFE;
float g_temperatura1wireA = 0xFFFE;
float g_temperatura1wireB = 0xFFFE;
unsigned int g_temperatura1wireAint = 0xFFFE;
unsigned int g_temperatura1wireBint = 0xFFFE;
unsigned long g_lastSensorRead = 0;

// prototipi funzioni
String command(String command);
void buzzer(int times);
void sendMessage(uint8_t msg[], int size);
void IRAM_ATTR pluvio_ISR();
void leggiSensori();  // Nuova funzione per lettura sensori

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
    digitalWrite(BOOST_EN, 1);  // ci diamo corrente DOPO aver constatato che sono presenti le forchette, altrimenti se c'è attaccata una cella di carico, la brucia...
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
        if (rs485risultati[0] != 0xFF) {
          Serial.println("Forchetta digitale presente su porta E");
          forchettaCdigitalePresente = 1;
          break;
        }
      }
      Serial.println("Forchetta analogica su porta C presente");
      forchettaCpresente = 1;
    }
    if (!digitalRead(PIN_FORK_D_PRESENZA)) {
      Serial.println("Lettura forchetta RS485 su porta F:");
      forkett.begin(FORKETT_BAUD, SERIAL_8N1, RS485_RX_2, RS485_TX_2);
      delay(100);
      for (int j = 0; j < 10; j++) {
        rs485(forkett, rs485risultati);
        if (rs485risultati[0] != 0xFF) {
          Serial.println("Forchetta digitale presente su porta F");
          forchettaDdigitalePresente = 1;
          break;
        }
      }
      Serial.println("Forchetta analogica su porta D presente");
      forchettaDpresente = 1;
    }
  }

  if (!forchettaCpresente && !forchettaDpresente && !forchettaCdigitalePresente && !forchettaDdigitalePresente) {
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

// ========== FUNZIONE LETTURA SENSORI ==========
void leggiSensori() {
  setCpuFrequencyMhz(240);

  esp_task_wdt_reset();  // Reset watchdog all'inizio

  uint32_t batteriaMedia = 0;
  for (int i = 10; i--;) {
    batteriaMedia += analogReadMilliVolts(PIN_BATTERY) * 2;
    delay(50);
  }
  g_batteria = batteriaMedia / 10;

  if (presenzaOneWireA || presenzaOneWireB || i2cPresenteA || i2cPresenteB) {
    Wire.begin();
    pinMode(IO_ENABLE, OUTPUT);
    digitalWrite(IO_ENABLE, 1);
    delay(100);

    esp_task_wdt_reset();  // Reset dopo init I2C

    if (!presenzaOneWireA) {
      digitalWrite(I2C_SELECT, 1);
      delay(500);
      if (i2cPresenteA) {
        uint16_t *valori = sht3x(SHT3X_ADDRESS);
        if (!sigfoxLora) {
          g_shtTempA = valori[0];
          g_shtHumA = valori[1];
        } else {
          g_shtTempA = valori[2];
          g_shtHumA = valori[3];
        }
      }
    } else {
      g_temperatura1wireA = read_1wire(sens1wireA, indirizzo1wireA, tipo1wireA);
      g_temperatura1wireAint = (int)(g_temperatura1wireA * 10);
    }

    esp_task_wdt_reset();  // Reset dopo lettura porta A

    if (!presenzaOneWireB) {
      digitalWrite(I2C_SELECT, 0);
      delay(500);
      if (i2cPresenteB) {
        uint16_t *valori = sht3x(SHT3X_ADDRESS);
        if (!sigfoxLora) {
          g_shtTempB = valori[0] | valori[5];
          g_shtHumB = valori[1];
        } else {
          g_shtTempB = valori[2];
          g_shtHumB = valori[3];
        }
      }
    } else {
      g_temperatura1wireB = read_1wire(sens1wireB, indirizzo1wireB, tipo1wireB);
      g_temperatura1wireBint = (int)(g_temperatura1wireB * 10);
    }

    Wire.end();
    digitalWrite(IO_ENABLE, 0);
  }

  delay(100);
  esp_task_wdt_reset();  // Reset prima letture forchette

  if (forchettaCpresente || forchettaDpresente || forchettaCdigitalePresente || forchettaDdigitalePresente) {
    digitalWrite(BOOST_SHTDWN, 1);
    digitalWrite(BOOST_EN, 1);
    delay(200);

    if (forchettaCdigitalePresente) {
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
      g_rs485TempE = accumuloTempE / 10;
      g_rs485HumE = accumuloHumE / 10;
    } else if (forchettaCpresente) {
      uint32_t mediaForchetta = 0;
      for (int i = 0; i < 10; i++) {
        mediaForchetta += analogRead(PIN_FORK_C);
        delay(50);
      }
      g_forchettaAnalogC = (mediaForchetta / 10) >> 2;
      if (g_forchettaAnalogC > 1000) g_forchettaAnalogC = 1000;
    }

    delay(300);
    esp_task_wdt_reset();  // Reset tra porta C e D

    if (forchettaDdigitalePresente) {
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
      g_rs485TempF = accumuloTempF / 10;
      g_rs485HumF = accumuloHumF / 10;
    } else if (forchettaDpresente) {
      uint32_t mediaForchetta = 0;
      for (int i = 0; i < 10; i++) {
        mediaForchetta += analogRead(PIN_FORK_D);
        delay(50);
      }
      g_forchettaAnalogD = (mediaForchetta / 10) >> 2;
      if (g_forchettaAnalogD > 1000) g_forchettaAnalogD = 1000;
    }

    digitalWrite(BOOST_SHTDWN, 0);
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
    g_peso1 = mediaBilancia - taraturaC;

    esp_task_wdt_reset();  // Reset tra bilancia C e D

    bilancia.begin(PIN_BIL_D, PIN_FORK_D);
    delay(100);

    mediaBilancia = 0;
    for (int i = 10; i--;) {
      mediaBilancia += bilancia.read();
    }
    g_peso2 = mediaBilancia - taraturaD;
  }

  digitalWrite(RS485_RE, 1);
  digitalWrite(RS485_DE, 0);
  digitalWrite(BOOST_EN, 0);
  digitalWrite(BOOST_SHTDWN, 0);

  setCpuFrequencyMhz(80);

  g_lastSensorRead = millis();

  esp_task_wdt_reset();  // Reset finale
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
      Serial.print("Era in sleep mode? ");
      Serial.println(eraInSleepMode ? "si" : "no");
      Serial.println(pluvioCount);
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
    // RACCOLTA DATI SENSORI
    /////////////////////////////////////

    Serial.print("Ciclo: ");
    Serial.println(contaCicli);

    // Prima lettura sensori
    leggiSensori();

    Serial.print("Lettura batteria (mV): ");
    Serial.println(g_batteria);

    /////////////////////////////////////
    // INVIO DATI VIA RADIO
    /////////////////////////////////////
    radio.flush();
    radio.end();
    delay(300);
    radio.begin(!sigfoxLora ? RADIO_BAUD : LORA_BAUD, SERIAL_8N1, RXpin, TXpin);
    delay(100);

    if (!sigfoxLora) {
      uint8_t msgS[12];
      msgS[0] = 0xA1;
      msgS[1] = 0;
      msgS[2] = g_forchettaAnalogC != 0xFFFE ? highByte(g_forchettaAnalogC) : highByte(g_rs485HumE);
      msgS[3] = g_forchettaAnalogC != 0xFFFE ? lowByte(g_forchettaAnalogC) : lowByte(g_rs485HumE);
      msgS[4] = g_forchettaAnalogD != 0xFFFE ? highByte(g_forchettaAnalogD) : highByte(g_rs485HumF);
      msgS[5] = g_forchettaAnalogD != 0xFFFE ? lowByte(g_forchettaAnalogD) : lowByte(g_rs485HumF);
      msgS[6] = g_shtTempA ? g_shtTempA : g_shtTempB;
      msgS[7] = g_shtHumA ? g_shtHumA : g_shtHumB;
      msgS[8] = 0;
      msgS[9] = 0;
      msgS[10] = g_batteria;
      msgS[11] = 0xED;

      Serial.println("Messaggio SigFox: ");
      for (int i = 0; i < 12; i++) {
        Serial.print(msgS[i], HEX);
        Serial.print("|");
      }
      Serial.println();
      delay(500);

      Serial.println("ID dispositivo SigFox:");
      Serial.println(command("AT$I=10\r"));
      delay(100);
      Serial.println("PAC dispositivo SigFox:");
      Serial.println(command("AT$I=11\r"));
      delay(100);
      sendMessage(msgS, 12);
    } else {
      uint8_t msgL[70];
      uint8_t numeroCasuale = (uint8_t)random(1, 255);

      msgL[0] = dispositivoID[0];
      msgL[1] = dispositivoID[1];
      msgL[2] = dispositivoID[2];
      msgL[3] = dispositivoID[3];
      msgL[4] = dispositivoID[4];
      msgL[5] = dispositivoID[5];

      msgL[6] = highByte(0xFF);
      msgL[7] = lowByte(0xFE);
      msgL[8] = highByte(0xFF);
      msgL[9] = lowByte(0xFE);
      msgL[10] = highByte(g_temperatura1wireAint);
      msgL[11] = lowByte(g_temperatura1wireAint);
      msgL[12] = highByte(0xFF);
      msgL[13] = lowByte(0xFE);
      msgL[14] = highByte(0xFF);
      msgL[15] = lowByte(0xFE);
      msgL[16] = highByte(0xFF);
      msgL[17] = lowByte(0xFE);

      msgL[18] = highByte(0xFF);
      msgL[19] = lowByte(0xFE);
      msgL[20] = highByte(0xFF);
      msgL[21] = lowByte(0xFE);
      msgL[22] = highByte(g_temperatura1wireBint);
      msgL[23] = lowByte(g_temperatura1wireBint);
      msgL[24] = highByte(pluvioCount);
      msgL[25] = lowByte(pluvioCount);
      msgL[26] = highByte(pluvioCount);
      msgL[27] = lowByte(pluvioCount);
      msgL[28] = highByte(0xFF);
      msgL[29] = lowByte(0xFE);

      msgL[30] = highByte(0xFF);
      msgL[31] = lowByte(0xFE);
      msgL[32] = highByte(0xFF);
      msgL[33] = lowByte(0xFE);
      msgL[34] = highByte(g_forchettaAnalogC);
      msgL[35] = lowByte(g_forchettaAnalogC);
      msgL[36] = highByte(0xFF);
      msgL[37] = lowByte(0xFE);
      msgL[38] = highByte(g_shtTempA);
      msgL[39] = lowByte(g_shtTempA);
      msgL[40] = highByte(g_shtHumA);
      msgL[41] = lowByte(g_shtHumA);
      msgL[42] = highByte(0xFF);
      msgL[43] = lowByte(0xFE);
      msgL[44] = highByte(g_pesoGrammi1);
      msgL[45] = lowByte(g_pesoGrammi1);
      msgL[46] = highByte(g_rs485TempE);
      msgL[47] = lowByte(g_rs485TempE);
      msgL[48] = highByte(g_rs485HumE);
      msgL[49] = lowByte(g_rs485HumE);

      msgL[50] = highByte(g_shtTempB);
      msgL[51] = lowByte(g_shtTempB);
      msgL[52] = highByte(g_shtHumB);
      msgL[53] = lowByte(g_shtHumB);
      msgL[54] = highByte(0xFF);
      msgL[55] = lowByte(0xFE);
      msgL[56] = highByte(g_pesoGrammi2);
      msgL[57] = lowByte(g_pesoGrammi2);
      msgL[58] = highByte(g_forchettaAnalogD);
      msgL[59] = lowByte(g_forchettaAnalogD);
      msgL[60] = highByte(g_rs485TempF);
      msgL[61] = lowByte(g_rs485TempF);
      msgL[62] = highByte(g_rs485HumF);
      msgL[63] = lowByte(g_rs485HumF);

      msgL[64] = highByte(g_batteria);
      msgL[65] = lowByte(g_batteria);
      msgL[66] = numeroCasuale;
      msgL[67] = 0;
      msgL[68] = 0xFF;
      msgL[69] = 0xFF;

      delay(100);

      Serial.println("Messaggio LoRa:");
      for (int i = 0; i < 70; i++) {
        Serial.print(msgL[i], HEX);
        Serial.print("|");
      }
      Serial.println();

      Serial.println("Invio messaggio LoRa...");
      radio.write(msgL, 70);
    }

    /////////////////////////////////////
    // BLOCCO ACCESS POINT WiFi - REAL TIME
    /////////////////////////////////////
    if (contaCicli == 0) {
      Serial.println("\n========================================");
      Serial.println("    ACCESS POINT WiFi ATTIVO");
      Serial.println("    MODALITÀ REAL-TIME");
      Serial.println("========================================");

      // Disabilita watchdog per evitare reset durante init WiFi
      esp_task_wdt_delete(NULL);

      WiFi.mode(WIFI_AP);
      delay(100);
      esp_task_wdt_reset();  // Reset watchdog

      WiFi.softAP("SENSOR_NODE");
      delay(100);
      esp_task_wdt_reset();  // Reset watchdog

      Serial.print("SSID: SENSOR_NODE\n");
      Serial.print("IP Address: ");
      Serial.println(WiFi.softAPIP());
      Serial.println("Password: nessuna (AP aperto)");
      Serial.println("\nAccedere da browser a: http://192.168.4.1");
      Serial.println("Timeout iniziale: 60 secondi");
      Serial.println("Una volta connesso, il WiFi resta attivo fino alla chiusura della pagina\n");

      WebServer server(80);
      bool clientConnesso = false;
      unsigned long lastClientRequest = 0;
      unsigned long lastSensorUpdate = 0;

      // ========== HANDLER PAGINA HTML PRINCIPALE ==========
      server.on("/", [&]() {
        String html = "<!DOCTYPE html><html><head>";
        html += "<meta charset='UTF-8'>";
        html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
        html += "<title>Sensor Node - Real Time</title>";
        html += "<style>";
        html += "body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f5f5f5}";
        html += ".container{max-width:800px;margin:0 auto;background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}";
        html += "h1{color:#2c3e50;margin:0 0 10px 0;font-size:24px;border-bottom:3px solid #3498db;padding-bottom:10px}";
        html += ".status{display:flex;align-items:center;margin-bottom:20px;padding:10px;background:#e8f4f8;border-radius:5px}";
        html += ".status-dot{width:12px;height:12px;border-radius:50%;margin-right:10px;background:#27ae60;animation:pulse 2s infinite}";
        html += "@keyframes pulse{0%,100%{opacity:1}50%{opacity:0.5}}";
        html += ".update-time{color:#7f8c8d;font-size:14px;margin-left:auto}";
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
        html += "<h1>📡 SENSOR NODE - Real Time</h1>";

        html += "<div class='status'>";
        html += "<span class='status-dot'></span>";
        html += "<strong>Connesso - Aggiornamento automatico</strong>";
        html += "<span class='update-time' id='lastUpdate'>Caricamento...</span>";
        html += "</div>";

        html += "<a href='/data.json' class='btn'>📥 Scarica JSON</a>";

        html += "<table id='dataTable'>";
        html += "<tr><th colspan='2' class='section-header'>📊 Informazioni Generali</th></tr>";
        html += "<tr><td>Tipo Radio</td><td class='value' id='radioType'>" + String(sigfoxLora ? "LoRa" : "SigFox") + "</td></tr>";
        if (sigfoxLora) {
          html += "<tr><td>ID Dispositivo</td><td class='value'>" + dispositivoID + "</td></tr>";
        }
        html += "<tr><td>Numero Ciclo</td><td class='value'>" + String(contaCicli) + "</td></tr>";
        html += "<tr><td>Batteria</td><td class='value' id='battery'>" + String(g_batteria) + " mV</td></tr>";

        html += "<tr><th colspan='2' class='section-header'>🔌 Porta A</th></tr>";
        html += "<tr><td>Tipo Sensore</td><td class='value' id='portAType'>...</td></tr>";
        html += "<tr><td>Dati</td><td class='value' id='portAData'>...</td></tr>";

        html += "<tr><th colspan='2' class='section-header'>🔌 Porta B</th></tr>";
        html += "<tr><td>Tipo Sensore</td><td class='value' id='portBType'>...</td></tr>";
        html += "<tr><td>Dati</td><td class='value' id='portBData'>...</td></tr>";
        html += "<tr><td>Pluviometro</td><td class='value' id='rain'>" + String(pluvioCount) + " impulsi</td></tr>";

        html += "<tr><th colspan='2' class='section-header'>🔌 Porta C (E)</th></tr>";
        html += "<tr><td>Tipo Sensore</td><td class='value' id='portCType'>...</td></tr>";
        html += "<tr><td>Dati</td><td class='value' id='portCData'>...</td></tr>";

        html += "<tr><th colspan='2' class='section-header'>🔌 Porta D (F)</th></tr>";
        html += "<tr><td>Tipo Sensore</td><td class='value' id='portDType'>...</td></tr>";
        html += "<tr><td>Dati</td><td class='value' id='portDData'>...</td></tr>";

        html += "</table>";

        html += "<div class='footer'>ESP32-S3 Sensor Node v2.1.0<br>Aggiornamento in tempo reale ogni 5 secondi</div>";
        html += "</div>";

        // JavaScript per AJAX polling
        html += "<script>";
        html += "function updateData(){";
        html += "fetch('/current_data').then(r=>r.json()).then(d=>{";
        html += "document.getElementById('battery').textContent=d.battery+' mV';";
        html += "document.getElementById('rain').textContent=d.rain+' impulsi';";

        // Porta A
        html += "if(d.portA.type=='onewire'){";
        html += "document.getElementById('portAType').textContent='OneWire';";
        html += "document.getElementById('portAData').textContent='Temp: '+d.portA.temp+' °C';";
        html += "}else if(d.portA.type=='i2c'){";
        html += "document.getElementById('portAType').textContent='I2C (SHT3x)';";
        html += "document.getElementById('portAData').textContent='Temp: '+d.portA.temp+' °C, Hum: '+d.portA.hum+' %';";
        html += "}else{document.getElementById('portAType').textContent='Nessuno';document.getElementById('portAData').textContent='N/D';}";

        // Porta B
        html += "if(d.portB.type=='onewire'){";
        html += "document.getElementById('portBType').textContent='OneWire';";
        html += "document.getElementById('portBData').textContent='Temp: '+d.portB.temp+' °C';";
        html += "}else if(d.portB.type=='i2c'){";
        html += "document.getElementById('portBType').textContent='I2C (SHT3x)';";
        html += "document.getElementById('portBData').textContent='Temp: '+d.portB.temp+' °C, Hum: '+d.portB.hum+' %';";
        html += "}else{document.getElementById('portBType').textContent='Nessuno';document.getElementById('portBData').textContent='N/D';}";

        // Porta C
        html += "if(d.portC.type=='rs485'){";
        html += "document.getElementById('portCType').textContent='Forchetta RS485';";
        html += "document.getElementById('portCData').textContent='Temp: '+d.portC.temp+' °C, Hum: '+d.portC.hum+' %';";
        html += "}else if(d.portC.type=='analog'){";
        html += "document.getElementById('portCType').textContent='Forchetta Analogica';";
        html += "document.getElementById('portCData').textContent='Valore: '+d.portC.value;";
        html += "}else if(d.portC.type=='load'){";
        html += "document.getElementById('portCType').textContent='Cella di Carico';";
        html += "document.getElementById('portCData').textContent='Peso: '+d.portC.weight+' g';";
        html += "}else{document.getElementById('portCType').textContent='Nessuno';document.getElementById('portCData').textContent='N/D';}";

        // Porta D
        html += "if(d.portD.type=='rs485'){";
        html += "document.getElementById('portDType').textContent='Forchetta RS485';";
        html += "document.getElementById('portDData').textContent='Temp: '+d.portD.temp+' °C, Hum: '+d.portD.hum+' %';";
        html += "}else if(d.portD.type=='analog'){";
        html += "document.getElementById('portDType').textContent='Forchetta Analogica';";
        html += "document.getElementById('portDData').textContent='Valore: '+d.portD.value;";
        html += "}else if(d.portD.type=='load'){";
        html += "document.getElementById('portDType').textContent='Cella di Carico';";
        html += "document.getElementById('portDData').textContent='Peso: '+d.portD.weight+' g';";
        html += "}else{document.getElementById('portDType').textContent='Nessuno';document.getElementById('portDData').textContent='N/D';}";

        html += "var now=new Date();";
        html += "document.getElementById('lastUpdate').textContent='Aggiornato: '+now.toLocaleTimeString();";
        html += "}).catch(e=>{console.error(e);clearInterval(updateInterval);document.querySelector('.status-dot').style.background='#e74c3c';document.querySelector('.status strong').textContent='Disconnesso';});";
        html += "}";
        html += "updateData();";
        html += "var updateInterval=setInterval(updateData,5000);";
        html += "</script>";

        html += "</body></html>";

        server.send(200, "text/html", html);
        clientConnesso = true;
        lastClientRequest = millis();
        Serial.println("✓ Client connesso - modalità real-time attiva");
      });

      // ========== HANDLER CURRENT DATA (AJAX) ==========
      server.on("/current_data", [&]() {
        String json = "{";
        json += "\"battery\":" + String(g_batteria) + ",";
        json += "\"rain\":" + String(pluvioCount) + ",";

        // Porta A
        json += "\"portA\":{";
        if (presenzaOneWireA) {
          json += "\"type\":\"onewire\",";
          json += "\"temp\":" + String(g_temperatura1wireA, 1);
        } else if (i2cPresenteA) {
          json += "\"type\":\"i2c\",";
          json += "\"temp\":" + String(g_shtTempA / 10.0, 1) + ",";
          json += "\"hum\":" + String(g_shtHumA);
        } else {
          json += "\"type\":\"none\"";
        }
        json += "},";

        // Porta B
        json += "\"portB\":{";
        if (presenzaOneWireB) {
          json += "\"type\":\"onewire\",";
          json += "\"temp\":" + String(g_temperatura1wireB, 1);
        } else if (i2cPresenteB) {
          json += "\"type\":\"i2c\",";
          json += "\"temp\":" + String(g_shtTempB / 10.0, 1) + ",";
          json += "\"hum\":" + String(g_shtHumB);
        } else {
          json += "\"type\":\"none\"";
        }
        json += "},";

        // Porta C
        json += "\"portC\":{";
        if (forchettaCdigitalePresente && g_rs485TempE != 0xFFFE) {
          json += "\"type\":\"rs485\",";
          json += "\"temp\":" + String(g_rs485TempE / 10.0, 1) + ",";
          json += "\"hum\":" + String(g_rs485HumE / 10.0, 1);
        } else if (forchettaCpresente && g_forchettaAnalogC != 0xFFFE) {
          json += "\"type\":\"analog\",";
          json += "\"value\":" + String(g_forchettaAnalogC);
        } else if (g_peso1 != 0xFFFE) {
          json += "\"type\":\"load\",";
          json += "\"weight\":" + String(g_pesoGrammi1);
        } else {
          json += "\"type\":\"none\"";
        }
        json += "},";

        // Porta D
        json += "\"portD\":{";
        if (forchettaDdigitalePresente && g_rs485TempF != 0xFFFE) {
          json += "\"type\":\"rs485\",";
          json += "\"temp\":" + String(g_rs485TempF / 10.0, 1) + ",";
          json += "\"hum\":" + String(g_rs485HumF / 10.0, 1);
        } else if (forchettaDpresente && g_forchettaAnalogD != 0xFFFE) {
          json += "\"type\":\"analog\",";
          json += "\"value\":" + String(g_forchettaAnalogD);
        } else if (g_peso2 != 0xFFFE) {
          json += "\"type\":\"load\",";
          json += "\"weight\":" + String(g_pesoGrammi2);
        } else {
          json += "\"type\":\"none\"";
        }
        json += "}";

        json += "}";

        server.send(200, "application/json", json);
        lastClientRequest = millis();
      });

      // ========== HANDLER JSON DOWNLOAD ==========
      server.on("/data.json", [&]() {
        String json = "{\n  \"device_info\": {\n";
        json += "    \"radio_type\": \"" + String(sigfoxLora ? "LoRa" : "SigFox") + "\",\n";
        if (sigfoxLora) {
          json += "    \"device_id\": \"" + dispositivoID + "\",\n";
        }
        json += "    \"cycle_number\": " + String(contaCicli) + ",\n";
        json += "    \"battery_mv\": " + String(g_batteria) + ",\n";
        json += "    \"timestamp_ms\": " + String(millis()) + "\n  },\n";

        json += "  \"port_a\": {";
        if (presenzaOneWireA) {
          json += "\"type\":\"onewire\",\"temperature_c\":" + String(g_temperatura1wireA, 2);
        } else if (i2cPresenteA) {
          json += "\"type\":\"i2c_sht3x\",\"temperature_c\":" + String(g_shtTempA / 10.0, 1) + ",\"humidity_pct\":" + String(g_shtHumA);
        } else {
          json += "\"type\":\"none\"";
        }
        json += "},\n";

        json += "  \"port_b\": {";
        if (presenzaOneWireB) {
          json += "\"type\":\"onewire\",\"temperature_c\":" + String(g_temperatura1wireB, 2) + ",";
        } else if (i2cPresenteB) {
          json += "\"type\":\"i2c_sht3x\",\"temperature_c\":" + String(g_shtTempB / 10.0, 1) + ",\"humidity_pct\":" + String(g_shtHumB) + ",";
        } else {
          json += "\"type\":\"none\",";
        }
        json += "\"rain_count\":" + String(pluvioCount);
        json += "},\n";

        json += "  \"port_c_e\": {";
        if (forchettaCdigitalePresente && g_rs485TempE != 0xFFFE) {
          json += "\"type\":\"rs485_fork\",\"temperature_c\":" + String(g_rs485TempE / 10.0, 1) + ",\"soil_humidity_pct\":" + String(g_rs485HumE / 10.0, 1);
        } else if (forchettaCpresente && g_forchettaAnalogC != 0xFFFE) {
          json += "\"type\":\"analog_fork\",\"value\":" + String(g_forchettaAnalogC);
        } else if (g_peso1 != 0xFFFE) {
          json += "\"type\":\"load_cell\",\"weight_raw\":" + String(g_peso1) + ",\"weight_g\":" + String(g_pesoGrammi1);
        } else {
          json += "\"type\":\"none\"";
        }
        json += "},\n";

        json += "  \"port_d_f\": {";
        if (forchettaDdigitalePresente && g_rs485TempF != 0xFFFE) {
          json += "\"type\":\"rs485_fork\",\"temperature_c\":" + String(g_rs485TempF / 10.0, 1) + ",\"soil_humidity_pct\":" + String(g_rs485HumF / 10.0, 1);
        } else if (forchettaDpresente && g_forchettaAnalogD != 0xFFFE) {
          json += "\"type\":\"analog_fork\",\"value\":" + String(g_forchettaAnalogD);
        } else if (g_peso2 != 0xFFFE) {
          json += "\"type\":\"load_cell\",\"weight_raw\":" + String(g_peso2) + ",\"weight_g\":" + String(g_pesoGrammi2);
        } else {
          json += "\"type\":\"none\"";
        }
        json += "}\n}";

        server.sendHeader("Content-Disposition", "attachment; filename=sensor_data.json");
        server.send(200, "application/json", json);
        lastClientRequest = millis();
        Serial.println("✓ File JSON scaricato");
      });

      server.begin();
      Serial.println("✓ Web server avviato\n");

      // ========== ATTESA PRIMA CONNESSIONE CON BEEP ==========
      unsigned long apStartTime = millis();
      unsigned long lastBeep = 0;

      Serial.println("Attesa connessione client (beep ogni 2 secondi)...");

      while (!clientConnesso && millis() - apStartTime < AP_INITIAL_TIMEOUT_MS) {
        server.handleClient();
        esp_task_wdt_reset();  // Reset watchdog per evitare timeout

        if (millis() - lastBeep >= 2000) {
          buzzer(1);
          lastBeep = millis();
          Serial.print(".");  // Indicatore visivo attesa
        }

        delay(10);
      }
      Serial.println();  // A capo dopo i punti

      if (!clientConnesso) {
        Serial.println("\n⚠ Timeout 60s - nessuna connessione");
        buzzer(1);
        server.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);

        // Ri-abilita watchdog dopo chiusura WiFi (ESP32 Core 3.x)
        esp_task_wdt_config_t wdt_config = {
          .timeout_ms = 30000,
          .idle_core_mask = 0,
          .trigger_panic = true
        };
        esp_task_wdt_init(&wdt_config);
        esp_task_wdt_add(NULL);

        delay(500);
        Serial.println("✓ Access Point chiuso");
        Serial.println("========================================\n");
      } else {
        // ========== LOOP REAL-TIME CON LETTURA SENSORI ==========
        Serial.println("\n========================================");
        Serial.println("✓✓✓ CLIENT CONNESSO ✓✓✓");
        Serial.println("Avvio modalità real-time");
        Serial.println("Pressione pagina mantiene WiFi attivo");
        Serial.println("Chiusura pagina → sleep dopo 15s");
        Serial.println("========================================\n");

        buzzer(2);   // 2 beep di conferma connessione
        delay(500);  // Pausa tra beep e avvio

        lastSensorUpdate = millis();  // Inizializza per prima lettura immediata

        Serial.println(">>> ENTRATO NEL LOOP REAL-TIME <<<\n");

        while (clientConnesso) {
          // Reset watchdog ad ogni iterazione del loop
          esp_task_wdt_reset();

          // Lettura sensori ogni 5 secondi
          if (millis() - lastSensorUpdate >= SENSOR_READ_INTERVAL_MS) {
            Serial.print("🔄 Aggiornamento sensori [");
            Serial.print(millis() / 1000);
            Serial.println("s]");
            leggiSensori();
            lastSensorUpdate = millis();
            Serial.println("   ✓ Sensori aggiornati");
          }

          // Gestione richieste HTTP
          server.handleClient();

          // Check timeout disconnessione
          unsigned long timeSinceLastRequest = millis() - lastClientRequest;
          if (timeSinceLastRequest > CLIENT_TIMEOUT_MS) {
            clientConnesso = false;
            Serial.println("\n⚠⚠⚠ Client disconnesso (timeout 15s) ⚠⚠⚠");
            Serial.print("Tempo dall'ultima richiesta: ");
            Serial.print(timeSinceLastRequest / 1000);
            Serial.println(" secondi");
          }

          // Debug periodico ogni 10 secondi
          static unsigned long lastDebug = 0;
          if (millis() - lastDebug > 10000) {
            Serial.print("📊 Status: WiFi attivo, ultimo polling ");
            Serial.print(timeSinceLastRequest / 1000);
            Serial.println("s fa");
            lastDebug = millis();
          }

          delay(100);
        }

        Serial.println("\n>>> USCITO DAL LOOP REAL-TIME <<<");

        // Chiusura
        Serial.println("\n🔴 Chiusura connessione...");
        buzzer(3);  // 3 beep di conferma chiusura
        server.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(100);
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
      delay(5000);
      radio.end();
      Serial.end();
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

    // gestione dinamica lightsleep
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
    esp_sleep_enable_timer_wakeup(tempoRimanente);
    uint64_t pinMask = (1ULL << INT1) | (1ULL << INT2);
    esp_sleep_enable_ext1_wakeup((gpio_num_t)pinMask, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_light_sleep_start();
  }
}

String command(String command) {
  String result = "";
  char output;

  radio.print(command);
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

void IRAM_ATTR pluvio_ISR() {
  eraInSleepMode = 1;
  unsigned long int ora = millis();
  if (ora - tempoUltimoImpulso > 100) {
    pluvioCount++;
    tempoUltimoImpulso = ora;
  }
}