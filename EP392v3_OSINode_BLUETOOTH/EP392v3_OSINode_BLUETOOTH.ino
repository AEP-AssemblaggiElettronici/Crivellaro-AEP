/*
  ============================================================================
  ESP32-S3 SENSOR NODE - Sistema di Acquisizione Dati Multi-Sensore
  ============================================================================
  
  VERSIONE: 3.0.1
  DATA: Ottobre 2025
  AUTORE: Fabio Crivellaro
  FILE: ESP32-S3_SENSOR_NODE_BLE_v3.0.1.ino
  
  DESCRIZIONE:
  Sistema di acquisizione dati da sensori multipli con trasmissione SigFox/LoRa
  e visualizzazione dati real-time tramite BLE (Bluetooth Low Energy).
  
  CHANGELOG:
  ----------
  v3.0.2 - fine Ottobre 2025
    + Aggiunta lettura sensori RS485 avanzati per conducibilità terreno e PH

  v3.0.1 - Ottobre 2025
    + Nome dispositivo BLE dinamico basato su ID modulo radio
    + LoRa: usa ID dispositivo (es. "L12345")
    + SigFox: usa ID ottenuto con comando "AT$I=10" (es. "4A5B6C7D")
    + Fallback a "SENSOR_NODE" se lettura ID fallisce
    + Rilevamento disconnessione migliorato (callback + fallback ogni 5s)
    + Timeout assoluto sessione BLE (5 minuti)
    + Chiusura forzata BLE con iterazione corretta su mappa peer devices
    - FIX: dispositivo bloccato dopo disconnessione
    - FIX: disconnessione immediata dopo connessione
    - FIX: consumo alto persistente
  
  v3.0.0 - Ottobre 2025
    + MIGRAZIONE A BLE (Bluetooth Low Energy) per ESP32-S3
    + Compatibilità nativa ESP32-S3 (no Bluetooth Classic)
    + Riduzione consumo energetico: 75-90% rispetto a Classic
    + Nordic UART Service (UUID standard universale)
    + BLE attivo SOLO nel primo ciclo
    + Visualizzazione dati sensori in formato testuale leggibile
    + Aggiornamento dati in tempo reale ogni 5 secondi
    + Timeout disconnessione: 15 secondi di inattività → sleep automatico
    + Beep ogni 2 secondi durante attesa connessione (max 60 secondi)
    + Nome dispositivo: "SENSOR_NODE"
    + App compatibili: "Serial Bluetooth Terminal" (Android), "nRF Connect"
    - RIMOSSO: BluetoothSerial (non supportato su ESP32-S3)
  
  v2.2.0 - Gennaio 2025
    + Bluetooth Serial attivo SOLO nel primo ciclo
    + Visualizzazione dati sensori in formato testuale leggibile
    + Aggiornamento dati in tempo reale ogni 5 secondi
    + Timeout disconnessione: 15 secondi di inattività → sleep automatico
    + Beep ogni 2 secondi durante attesa connessione (max 60 secondi)
    + Nome dispositivo: "SENSOR_NODE"
    + Compatibile con modulo LoRa (nessun conflitto, basso consumo ~40mA)
  
  v1.0.0 - Agosto-Settembre 2025
    - Versione originale con supporto radio SigFox/LoRa
    - Lettura sensori: OneWire (A/B), I2C SHT3x (A/B), RS485 (E/F)
    - Forchette analogiche umidità terreno (C/D)
    - Celle di carico per peso (C/D)
    - Pluviometro con interrupt su INT1/INT2
    - Gestione light sleep dinamico
  
  NOTE HARDWARE:
  - ESP32-S3: supporta SOLO BLE (no Bluetooth Classic)
  - Sensori OneWire vanno sulle porte A e B (NON C e D come ATSAMD21)
  - Alimentazione ESP32-S3 è 3V (NON 3.3V!)
  - RS485: verificare funzionamento forchette (rimosse 2 resistenze)
  
  BLE INFO:
  - Service UUID: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E (Nordic UART)
  - TX UUID:      6E400003-B5A3-F393-E0A9-E50E24DCCA9E (ESP32 → Phone)
  - RX UUID:      6E400002-B5A3-F393-E0A9-E50E24DCCA9E (Phone → ESP32)
  - Consumo medio: 2-5 mA idle, 10-15 mA trasmissione (vs 40-120 mA Classic)
  
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

// ========== LIBRERIE BLE ==========
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ========== CONFIGURAZIONE BLE ==========
#define BLE_DEVICE_NAME "SENSOR_NODE"    // Nome dispositivo BLE
#define BLE_TIMEOUT_MS 60000             // Timeout attesa connessione: 60 secondi
#define BLE_DISCONNECT_TIMEOUT_MS 15000  // Timeout dopo disconnessione: 15 secondi
#define BLE_UPDATE_INTERVAL_MS 5000      // Aggiornamento dati ogni 5 secondi
#define BLE_BEEP_INTERVAL_MS 2000        // Beep ogni 2 secondi

// UUID Nordic UART Service (standard universale)
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // ESP32 → Phone
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Phone → ESP32

// ========== VARIABILI BLE ==========
BLEServer *pServer = nullptr;
BLECharacteristic *pTxCharacteristic = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// ========== HARDWARE ==========
File filer;
HardwareSerial radio(1);
HardwareSerial forkett(2);
OneWire sens1wireA(INT1);
OneWire sens1wireB(INT2);
HX711 bilancia;

// ========== VARIABILI GLOBALI ==========
String dispositivoID = "";
String nomeDispositivoBLE = "SENSOR_NODE";  // Nome BLE dinamico (default fallback)
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
unsigned long taraturaC;
unsigned long taraturaD;
volatile unsigned long int pluvioCount = 0;
volatile unsigned long int tempoUltimoImpulso = 0;
volatile bool eraInSleepMode = 0;
bool i2cPresenteA = 0;
bool i2cPresenteB = 0;
uint8_t rs485risultati[11] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t rs485risultati2[11] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t rs485_estesaRisultati[19] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t rs485_estesaRisultati2[19] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// ========== VARIABILI SENSORI (per BLE real-time) ==========
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
uint16_t g_rs485PiaccaE = 0xFFFE;
uint16_t g_rs485ConducibilitaE = 0xFFFE;
uint16_t g_rs485PiaccaF = 0xFFFE;
uint16_t g_rs485ConducibilitaF = 0xFFFE;
uint16_t g_shtTempA = 0xFF;
uint16_t g_shtHumA = 0xFF;
uint16_t g_shtTempB = 0xFFFE;
uint16_t g_shtHumB = 0xFFFE;
float g_temperatura1wireA = 0xFFFE;
float g_temperatura1wireB = 0xFFFE;
unsigned int g_temperatura1wireAint = 0xFFFE;
unsigned int g_temperatura1wireBint = 0xFFFE;

// ========== PROTOTIPI FUNZIONI ==========
String command(String command);
void buzzer(int times);
void sendMessage(uint8_t msg[], int size);
void IRAM_ATTR pluvio_ISR();
void leggiSensori();
String generaDatiTestuali();
void inviaDataBLE(String data);

// ========== CALLBACKS BLE ==========
// Callback per gestire connessione/disconnessione BLE
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("\n✓✓✓ CLIENT CONNESSO VIA BLE ✓✓✓");
    buzzer(2);  // 2 beep di conferma
  }

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    Serial.println("\n⚠ Client BLE disconnesso");
  }
};

// Callback per ricevere dati dal telefono (opzionale, non usato in questo progetto)
// Versione semplificata senza gestione dati per evitare conflitti di tipo
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    // Callback vuota - non serve ricevere dati dal telefono in questo progetto
    // Il progetto invia solo dati sensori: ESP32 → Telefono
  }
};

// ========== SETUP ==========
void setup() {
  for (int i = 0; i < 7; i++) {
    pinMode(unusedPins[i], INPUT);
    digitalWrite(unusedPins[i], 0);
  }

  setCpuFrequencyMhz(80);

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

  if (!forchettaCpresente && !forchettaDpresente && !forchettaCdigitalePresente && !forchettaDdigitalePresente && !forchettaCdigitaleEstesaPresente && !forchettaDdigitaleEstesaPresente) {
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
  if (rispostaModulo == "" || rispostaModulo.length() != 18) {
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

    // Pulisci l'ID da caratteri extra
    dispositivoID.trim();
    dispositivoID.replace("\r", "");
    dispositivoID.replace("\n", "");

    Serial.print("ID dispositivo in memoria: ");
    Serial.print(dispositivoID);
    Serial.println();
    Serial.println("Cambiare ID e protocollo dispositivo? (premere entro 5 secondi 's' o premere qualsiasi altro tasto per procedere)");
    unsigned int tempoEditDispositivo = millis();
    while (!Serial.available() && millis() - tempoEditDispositivo < 5000)
      ;
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
    } else {
      // Se NON ha premuto nulla, chiudi SPIFFS qui
      SPIFFS.end();
    }

    // SEMPRE esegui questo blocco, indipendentemente da cosa è successo sopra
    // Imposta nome BLE per LoRa
    dispositivoID.trim();  // Pulizia finale
    Serial.print("DEBUG - dispositivoID dopo lettura: '");
    Serial.print(dispositivoID);
    Serial.print("' (lunghezza: ");
    Serial.print(dispositivoID.length());
    Serial.println(")");

    if (dispositivoID.length() > 0) {
      nomeDispositivoBLE = dispositivoID;
      Serial.print("Nome dispositivo BLE (LoRa): ");
      Serial.println(nomeDispositivoBLE);
    } else {
      Serial.println("⚠ ID LoRa vuoto, uso nome BLE default: SENSOR_NODE");
    }

  } else {
    Serial.println("Modulo SigFox installato");

    // Leggi ID SigFox per nome BLE
    Serial.println("Lettura ID SigFox per nome BLE...");
    String idSigfox = command("AT$I=10\r");
    // Pulisci la stringa (rimuovi newline, carriage return, spazi)
    idSigfox.trim();
    idSigfox.replace("\r", "");
    idSigfox.replace("\n", "");

    if (idSigfox.length() > 0 && idSigfox.length() <= 29) {
      nomeDispositivoBLE = idSigfox;
      Serial.print("Nome dispositivo BLE: ");
      Serial.println(nomeDispositivoBLE);
    } else {
      Serial.println("ID SigFox non valido, uso nome default");
    }

    radio.flush();
    radio.end();
  }

  digitalWrite(BOOST_EN, 0);
  digitalWrite(BOOST_SHTDWN, 0);

  attachInterrupt(INT1, pluvio_ISR, FALLING);
  attachInterrupt(INT2, pluvio_ISR, FALLING);

  delay(1000);
  Serial.println("DEBUG - fine setup");
}

// ========== FUNZIONE LETTURA SENSORI ==========
void leggiSensori() {
  setCpuFrequencyMhz(240);

  uint32_t batteriaMedia = 0;
  for (int i = 10; i--;) {
    batteriaMedia += analogReadMilliVolts(PIN_BATTERY) * 2;
    delay(50);
  }
  g_batteria = ((batteriaMedia / 10) * 1.03) + 40;

  if (presenzaOneWireA || presenzaOneWireB || i2cPresenteA || i2cPresenteB) {
    Wire.begin();
    pinMode(IO_ENABLE, OUTPUT);
    digitalWrite(IO_ENABLE, 1);
    delay(100);

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
        g_rs485TempE = accumuloTempE;
        g_rs485HumE = accumuloHumE;
        g_rs485PiaccaE = accumuloPiaccaE;
        g_rs485ConducibilitaE = accumuloConducibilitaE;
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
        g_rs485TempE = accumuloTempE / 10;
        g_rs485HumE = accumuloHumE / 10;
      }
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
        g_rs485TempF = accumuloTempF;
        g_rs485HumF = accumuloHumF;
        g_rs485PiaccaF = accumuloPiaccaF;
        g_rs485ConducibilitaF = accumuloConducibilitaF;
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
    g_peso1 = mediaBilancia - taraturaC;

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
}

// ========== FUNZIONE GENERAZIONE DATI TESTUALI ==========
String generaDatiTestuali() {
  String output = "";

  output += "=======================================\n";
  output += "     ESP32-S3 SENSOR NODE v3.0.1\n";
  output += "     (BLE - Bluetooth Low Energy)\n";
  output += "=======================================\n";
  output += "Radio: " + String(sigfoxLora ? "LoRa" : "SigFox") + "\n";
  if (sigfoxLora && dispositivoID.length() > 0) {
    output += "ID: " + dispositivoID + "\n";
  }
  output += "Ciclo: " + String(contaCicli) + "\n";
  output += "Batteria: " + String(g_batteria) + " mV\n";
  output += "---------------------------------------\n";

  // PORTA A
  output += "PORTA A:\n";
  if (presenzaOneWireA) {
    output += "  Tipo: OneWire\n";
    if (g_temperatura1wireAint != 0xFFFE) {
      output += "  Temp: " + String(g_temperatura1wireA, 1) + " C\n";
    } else {
      output += "  Temp: N/D\n";
    }
  } else if (i2cPresenteA) {
    output += "  Tipo: I2C (SHT3x)\n";
    if (g_shtTempA != 0xFF && g_shtTempA != 0xFFFE) {
      output += "  Temp: " + String(g_shtTempA / 10.0, 1) + " C\n";
      output += "  Hum:  " + String(g_shtHumA) + " %\n";
    } else {
      output += "  Dati: N/D\n";
    }
  } else {
    output += "  Nessun sensore\n";
  }

  // PORTA B
  output += "PORTA B:\n";
  if (presenzaOneWireB) {
    output += "  Tipo: OneWire\n";
    if (g_temperatura1wireBint != 0xFFFE) {
      output += "  Temp: " + String(g_temperatura1wireB, 1) + " C\n";
    } else {
      output += "  Temp: N/D\n";
    }
  } else if (i2cPresenteB) {
    output += "  Tipo: I2C (SHT3x)\n";
    if (g_shtTempB != 0xFFFE) {
      output += "  Temp: " + String(g_shtTempB / 10.0, 1) + " C\n";
      output += "  Hum:  " + String(g_shtHumB) + " %\n";
    } else {
      output += "  Dati: N/D\n";
    }
  } else {
    output += "  Nessun sensore\n";
  }
  output += "  Pluvio: " + String(pluvioCount) + " impulsi\n";

  // PORTA C (E)
  output += "PORTA C (E):\n";
  if (forchettaCdigitalePresente) {
    output += "  Tipo: Forchetta RS485\n";
    if (g_rs485TempE != 0xFFFE) {
      output += "  Temp: " + String(g_rs485TempE / 10.0, 1) + " C\n";
      output += "  Hum:  " + String(g_rs485HumE / 10.0, 1) + " %\n";
    } else {
      output += "  Dati: N/D\n";
    }
  } else if (forchettaCdigitaleEstesaPresente) {
    output += "  Tipo: Forchetta RS485 estesa\n";
    if (g_rs485TempE != 0xFFFE) {
      output += "  Temp: " + String(g_rs485TempE / 10.0, 1) + " C\n";
      output += "  Hum:  " + String(g_rs485HumE / 10.0, 1) + " %\n";
      output += "  PH:   " + String(g_rs485PiaccaE / 10.0, 1) + "\n";
      output += "  Cond: " + String(g_rs485ConducibilitaE / 10.0, 1) + "us/cm\n";
    } else {
      output += "  Dati: N/D\n";
    }
  } else if (forchettaCpresente) {
    output += "  Tipo: Forchetta Analogica\n";
    if (g_forchettaAnalogC != 0xFFFE) {
      output += "  Val:  " + String(g_forchettaAnalogC) + " / 1000\n";
    } else {
      output += "  Val:  N/D\n";
    }
  } else if (g_peso1 != 0xFFFE) {
    output += "  Tipo: Cella di Carico\n";
    output += "  Peso raw: " + String(g_peso1) + "\n";
    if (g_pesoGrammi1 != 0xFFFE) {
      output += "  Peso: " + String(g_pesoGrammi1) + " g\n";
    }
  } else {
    output += "  Nessun sensore\n";
  }

  // PORTA D (F)
  output += "PORTA D (F):\n";
  if (forchettaDdigitalePresente) {
    output += "  Tipo: Forchetta RS485\n";
    if (g_rs485TempF != 0xFFFE) {
      output += "  Temp: " + String(g_rs485TempF / 10.0, 1) + " C\n";
      output += "  Hum:  " + String(g_rs485HumF / 10.0, 1) + " %\n";
    } else {
      output += "  Dati: N/D\n";
    }
  } else if (forchettaDdigitaleEstesaPresente) {
    output += "  Tipo: Forchetta RS485 estesa\n";
    if (g_rs485TempF != 0xFFFE) {
      output += "  Temp: " + String(g_rs485TempF / 10.0, 1) + " C\n";
      output += "  Hum:  " + String(g_rs485HumF / 10.0, 1) + " %\n";
      output += "  PH:   " + String(g_rs485PiaccaF / 10.0, 1) + "\n";
      output += "  Cond: " + String(g_rs485ConducibilitaF / 10.0, 1) + "us/cm\n";
    } else {
      output += "  Dati: N/D\n";
    }
  } else if (forchettaDpresente) {
    output += "  Tipo: Forchetta Analogica\n";
    if (g_forchettaAnalogD != 0xFFFE) {
      output += "  Val:  " + String(g_forchettaAnalogD) + " / 1000\n";
    } else {
      output += "  Val:  N/D\n";
    }
  } else if (g_peso2 != 0xFFFE) {
    output += "  Tipo: Cella di Carico\n";
    output += "  Peso raw: " + String(g_peso2) + "\n";
    if (g_pesoGrammi2 != 0xFFFE) {
      output += "  Peso: " + String(g_pesoGrammi2) + " g\n";
    }
  } else {
    output += "  Nessun sensore\n";
  }

  return output;
}

// ========== FUNZIONE INVIO DATI BLE ==========
void inviaDataBLE(String data) {
  if (deviceConnected && pTxCharacteristic != nullptr) {
    // BLE può inviare max 20 bytes per pacchetto (caratteristiche standard)
    // Dividiamo il messaggio in chunk se necessario
    int maxChunkSize = 20;
    int dataLen = data.length();

    for (int i = 0; i < dataLen; i += maxChunkSize) {
      String chunk = data.substring(i, min(i + maxChunkSize, dataLen));
      pTxCharacteristic->setValue(chunk.c_str());
      pTxCharacteristic->notify();
      delay(10);  // Piccolo delay tra i pacchetti per evitare overflow
    }
  }
}

// ========== LOOP PRINCIPALE ==========
void loop() {
  delay(200);
  static uint64_t cicloDurataUs = !sigfoxLora ? 900ULL * 1000000ULL : 480ULL * 1000000ULL;
  static uint64_t cicloInizio = 0;

  if (!eraInSleepMode) {

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

    uint16_t batteria = 0;
    uint16_t forchettaAnalogC = 0xFFFE;
    uint16_t forchettaAnalogD = 0xFFFE;
    long peso1 = 0xFFFE;
    long peso2 = 0xFFFE;
    long pesoGrammi1 = 0xFFFE;
    long pesoGrammi2 = 0xFFFE;
    uint16_t rs485TempE = 0xFFFE;
    uint16_t rs485HumE = 0xFFFE;
    uint16_t rs485TempF = 0xFFFE;
    uint16_t rs485HumF = 0xFFFE;
    uint8_t rs485risultati[11] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t rs485risultati2[11] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uint16_t shtTempA = 0xFF;
    uint16_t shtHumA = 0xFF;
    uint16_t shtTempB = 0xFFFE;
    uint16_t shtHumB = 0xFFFE;
    float temperatura1wireA = 0xFFFE;
    float temperatura1wireB = 0xFFFE;
    unsigned int temperatura1wireAint = 0xFFFE;
    unsigned int temperatura1wireBint = 0xFFFE;

    Serial.print("Ciclo: ");
    Serial.print(contaCicli);
    Serial.println();

    // Prima lettura sensori
    leggiSensori();

    batteria = g_batteria;
    forchettaAnalogC = g_forchettaAnalogC;
    forchettaAnalogD = g_forchettaAnalogD;
    peso1 = g_peso1;
    peso2 = g_peso2;
    pesoGrammi1 = g_pesoGrammi1;
    pesoGrammi2 = g_pesoGrammi2;
    rs485TempE = g_rs485TempE;
    rs485HumE = g_rs485HumE;
    rs485TempF = g_rs485TempF;
    rs485HumF = g_rs485HumF;
    shtTempA = g_shtTempA;
    shtHumA = g_shtHumA;
    shtTempB = g_shtTempB;
    shtHumB = g_shtHumB;
    temperatura1wireA = g_temperatura1wireA;
    temperatura1wireB = g_temperatura1wireB;
    temperatura1wireAint = g_temperatura1wireAint;
    temperatura1wireBint = g_temperatura1wireBint;

    Serial.print("Lettura batteria (mV): ");
    Serial.println(batteria);

    /////////////////////////////////////
    // INVIO DATI VIA RADIO
    /////////////////////////////////////
    radio.flush();
    radio.end();
    delay(300);
    radio.begin(!sigfoxLora ? RADIO_BAUD : LORA_BAUD, SERIAL_8N1, RXpin, TXpin);
    delay(100);
    uint16_t randomDelay = random(1, 6000);

    if (!sigfoxLora) {
      uint8_t msgS[12];
      msgS[0] = 0xA1;
      msgS[1] = 0;
      msgS[2] = forchettaAnalogC != 0xFFFE ? highByte(forchettaAnalogC) : highByte(rs485HumE);
      msgS[3] = forchettaAnalogC != 0xFFFE ? lowByte(forchettaAnalogC) : lowByte(rs485HumE);
      msgS[4] = forchettaAnalogD != 0xFFFE ? highByte(forchettaAnalogD) : highByte(rs485HumF);
      msgS[5] = forchettaAnalogD != 0xFFFE ? lowByte(forchettaAnalogD) : lowByte(rs485HumF);
      msgS[6] = shtTempA ? shtTempA : shtTempB;
      msgS[7] = shtHumA ? shtHumA : shtHumB;
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
      Serial.println(command("AT$I=10\r"));
      delay(100);
      Serial.println("PAC dispositivo SigFox:");
      Serial.println(command("AT$I=11\r"));
      delay(randomDelay);
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

      //-----------------------------SEGNALI DEL CONNETTORE A----------------------------------------------
      msgL[6] = highByte(0xFF);  // SEGNALE A1 tensiometro 60
      msgL[7] = lowByte(0xFE);

      msgL[8] = highByte(0xFF);  // SEGNALE A2  lux
      msgL[9] = lowByte(0xFE);

      msgL[10] = highByte(g_temperatura1wireAint);  // A3 SEGNALE futuro
      msgL[11] = lowByte(g_temperatura1wireAint);

      msgL[12] = highByte(g_rs485PiaccaE);  //  A4 SEGNALE futuro
      msgL[13] = lowByte(g_rs485PiaccaE);

      msgL[14] = highByte(g_rs485ConducibilitaE);  // A5 SEGNALE futuro
      msgL[15] = lowByte(g_rs485ConducibilitaE);

      msgL[16] = highByte(g_rs485PiaccaF);  // A6 SEGNALE futuro
      msgL[17] = lowByte(g_rs485PiaccaF);

      //-----------------------------SEGNALI DEL CONNETTORE B----------------------------------------------
      msgL[18] = highByte(0xFF);  // SEGNALE B7  tensiometro 60
      msgL[19] = lowByte(0xFE);

      msgL[20] = highByte(0xFF);  // SEGNALE B8 Lux
      msgL[21] = lowByte(0xFE);

      msgL[22] = highByte(g_temperatura1wireBint);  // SEGNALE B9 Temperatura
      msgL[23] = lowByte(g_temperatura1wireBint);

      msgL[24] = highByte(pluvioCount);  // SEGNALE B10 Pluviometro
      msgL[25] = lowByte(pluvioCount);

      msgL[26] = highByte(pluvioCount);  // SEGNALE B11 Drenato
      msgL[27] = lowByte(pluvioCount);

      msgL[28] = highByte(g_rs485ConducibilitaF);  // B12 SEGNALE futuro
      msgL[29] = lowByte(g_rs485ConducibilitaF);

      //-----------------------------SEGNALI DEL CONNETTORE C----------------------------------------------
      msgL[30] = highByte(0xFF);  // SEGNALE C13 Temperatura
      msgL[31] = lowByte(0xFE);

      msgL[32] = highByte(0xFF);  // SEGNALE C14 anemometro
      msgL[33] = lowByte(0xFE);

      msgL[34] = highByte(g_forchettaAnalogC);  // C15 forchetta analogica umidità
      msgL[35] = lowByte(g_forchettaAnalogC);

      msgL[36] = highByte(0xFF);  // SEGNALE 16  analog ( PAR, Soil Moist, Press, PH, Leaf WET)
      msgL[37] = lowByte(0xFE);

      msgL[38] = highByte(g_shtTempA);  // SEGNALE C17  TEMP_C
      msgL[39] = lowByte(g_shtTempA);

      msgL[40] = highByte(g_shtHumA);  // SEGNALE C18  UR_C
      msgL[41] = lowByte(g_shtHumA);

      msgL[42] = highByte(0xFF);  // SEGNALE C19  EC
      msgL[43] = lowByte(0xFE);

      msgL[44] = highByte(g_peso1);  // C20 SEGNALE PESO
      msgL[45] = lowByte(g_peso1);

      msgL[46] = highByte(g_rs485TempE);  // C21 rs485 temperatura
      msgL[47] = lowByte(g_rs485TempE);

      msgL[48] = highByte(g_rs485HumE);  // C22 rs485 umidità
      msgL[49] = lowByte(g_rs485HumE);

      //-----------------------------SEGNALI DEL CONNETTORE D----------------------------------------------
      msgL[50] = highByte(g_shtTempB);  // SEGNALE D23 TEMP_D
      msgL[51] = lowByte(g_shtTempB);

      msgL[52] = highByte(g_shtHumB);  // SEGNALE D24 UR_D
      msgL[53] = lowByte(g_shtHumB);

      msgL[54] = highByte(0xFF);  // SEGNALE 25   ( PAR, Soil Moist, Press, PH, Leaf WET)
      msgL[55] = lowByte(0xFE);

      msgL[56] = highByte(g_peso2);  // SEGNALE D26  PESO
      msgL[57] = lowByte(g_peso2);

      msgL[58] = highByte(g_forchettaAnalogD);  // SEGNALE D27 forchetta analogica umidità terreno
      msgL[59] = lowByte(g_forchettaAnalogD);

      msgL[60] = highByte(g_rs485TempF);  // D28 rs485 temperatura
      msgL[61] = lowByte(g_rs485TempF);

      msgL[62] = highByte(g_rs485HumF);  // D29 rs485 umidità
      msgL[63] = lowByte(g_rs485HumF);

      //----------------------------- TRASMISSIONE ----------------------------------------------
      msgL[64] = highByte(g_batteria);  // SEGNALE BAT30
      msgL[65] = lowByte(g_batteria);

      msgL[66] = numeroCasuale;  // CODICE RANDOM TRASMISSIONE
      msgL[67] = 0;              // invio caratteri di fine messaggio
      msgL[68] = 0xFF;
      msgL[69] = 0xFF;

      pinMode(PIN_SIGFOX_RESET, OUTPUT);  // sveglia il modulo LoRa
      digitalWrite(PIN_SIGFOX_RESET, 0);
      delay(100);

      Serial.println("Messaggio LoRa:");
      for (int i = 0; i < 70; i++) {
        Serial.print(msgL[i], HEX);
        Serial.print("|");
      }
      Serial.println();

      delay(randomDelay);
      Serial.println("Invio messaggio LoRa...");
      radio.write(msgL, 70);
      delay(1000);
    }

    /////////////////////////////////////
    // BLOCCO BLE - REAL TIME
    /////////////////////////////////////
    if (contaCicli == 0) {
      Serial.println("\n========================================");
      Serial.println("    BLE (BLUETOOTH LOW ENERGY) ATTIVO");
      Serial.println("    MODALITA' REAL-TIME");
      Serial.println("========================================");
      Serial.print("Nome dispositivo: ");
      Serial.println(nomeDispositivoBLE);
      Serial.println("App suggerite:");
      Serial.println("  Android: Serial Bluetooth Terminal");
      Serial.println("  Android/iOS: nRF Connect");
      Serial.println("\nInizializzazione BLE...");

      // Inizializza BLE con nome dinamico (ID LoRa o SigFox)
      BLEDevice::init(nomeDispositivoBLE.c_str());

      // Crea BLE Server
      pServer = BLEDevice::createServer();
      pServer->setCallbacks(new MyServerCallbacks());

      // Crea BLE Service (Nordic UART)
      BLEService *pService = pServer->createService(SERVICE_UUID);

      // Crea Caratteristica TX (ESP32 → Phone)
      pTxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_TX,
        BLECharacteristic::PROPERTY_NOTIFY);
      pTxCharacteristic->addDescriptor(new BLE2902());

      // Crea Caratteristica RX (Phone → ESP32) - opzionale
      BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_RX,
        BLECharacteristic::PROPERTY_WRITE);
      pRxCharacteristic->setCallbacks(new MyCallbacks());

      // Avvia servizio
      pService->start();

      // Avvia advertising
      BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
      pAdvertising->addServiceUUID(SERVICE_UUID);
      pAdvertising->setScanResponse(true);
      pAdvertising->setMinPreferred(0x06);
      pAdvertising->setMinPreferred(0x12);
      BLEDevice::startAdvertising();

      Serial.println("✓ BLE inizializzato e advertising attivo");
      Serial.println("\nAttesa connessione (max 60 secondi)...");
      Serial.println("Beep ogni 2 secondi durante attesa\n");

      // Attesa connessione con beep
      unsigned long bleStartTime = millis();
      unsigned long lastBeep = 0;
      bool clientConnesso = false;

      while (!deviceConnected && millis() - bleStartTime < BLE_TIMEOUT_MS) {
        if (millis() - lastBeep >= BLE_BEEP_INTERVAL_MS) {
          buzzer(1);
          lastBeep = millis();
          Serial.print(".");
        }
        delay(10);
      }
      Serial.println();

      if (deviceConnected) {
        clientConnesso = true;

        // Messaggio di benvenuto
        delay(500);  // Piccolo delay per stabilizzare connessione
        String welcome = "\n*** Connesso a " + nomeDispositivoBLE + " ***\n";
        welcome += "ESP32-S3 SENSOR NODE v3.0.0\n";
        welcome += "BLE - Bluetooth Low Energy\n";
        welcome += "Aggiornamento dati ogni 5 secondi\n";
        welcome += "Chiudi l'app per terminare\n\n";
        inviaDataBLE(welcome);

        // Loop real-time
        unsigned long lastUpdate = 0;
        unsigned long lastDisconnectCheck = millis();
        unsigned long loopStartTime = millis();

        Serial.println(">>> MODALITA' REAL-TIME BLE ATTIVA <<<\n");

        while (clientConnesso) {
          // CONTROLLO DISCONNESSIONE INTELLIGENTE

          // Metodo principale: callback deviceConnected (più affidabile)
          if (!deviceConnected) {
            Serial.println("\n⚠ Client disconnesso (callback)");
            clientConnesso = false;
            break;
          }

          // Metodo secondario: getConnectedCount() ogni 5 secondi (fallback)
          // Non controlliamo subito per dare tempo alla connessione di stabilizzarsi
          if (millis() - lastDisconnectCheck > 5000) {
            if (pServer->getConnectedCount() == 0) {
              Serial.println("\n⚠ Client disconnesso (server count)");
              clientConnesso = false;
              break;
            }
            lastDisconnectCheck = millis();
          }

          // Timeout assoluto sessione (5 minuti = sicurezza)
          if (millis() - loopStartTime > 300000) {
            Serial.println("\n⚠ Timeout assoluto 5 minuti - chiusura forzata");
            clientConnesso = false;
            break;
          }

          // Aggiornamento dati ogni 5 secondi
          if (millis() - lastUpdate >= BLE_UPDATE_INTERVAL_MS) {
            Serial.println("Aggiornamento dati sensori...");
            leggiSensori();

            String datiTestuali = generaDatiTestuali();
            inviaDataBLE(datiTestuali);

            lastUpdate = millis();
            Serial.println("✓ Dati inviati via BLE");

            // Debug: stampa stato connessione
            Serial.print("  [DEBUG] deviceConnected=");
            Serial.print(deviceConnected);
            Serial.print(" | ConnectedCount=");
            Serial.println(pServer->getConnectedCount());
          }

          delay(100);
        }

        Serial.println("\n>>> USCITO DAL LOOP REAL-TIME BLE <<<");
        buzzer(3);  // 3 beep di conferma chiusura

      } else {
        Serial.println("\n⚠ Timeout 60s - nessuna connessione BLE");
        buzzer(1);
      }

      // CHIUSURA COMPLETA E FORZATA BLE
      Serial.println("Chiusura BLE in corso...");

      // Stop advertising (importante!)
      BLEDevice::getAdvertising()->stop();
      delay(100);

      // Disconnetti tutti i client forzatamente (metodo corretto)
      if (pServer->getConnectedCount() > 0) {
        Serial.println("Disconnessione forzata client...");
        // Itera su tutti i client connessi
        std::map<uint16_t, conn_status_t> peerDevices = pServer->getPeerDevices(true);
        for (auto const &entry : peerDevices) {
          pServer->disconnect(entry.first);  // entry.first è il conn_id
        }
        delay(500);
      }

      // Deinit completo BLE
      BLEDevice::deinit(true);
      delay(500);

      Serial.println("✓ BLE chiuso completamente");
      Serial.println("========================================\n");
    }
    /////////////////////////////////////
    // FINE BLOCCO BLE
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

// ========== FUNZIONI AUSILIARIE ==========

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