/*
  Firmware abilitato per la trasmissione radio con SigFox o Lora.
  - Il nome del dispositivo prevede due formati LAAxxx e SFAAxx, dove SF/L è il tipo di trasmittente, AA è l'anno e x il numero di scheda
  - Le porte C e D della scheda sono abilitate alle forchette da 12 volt (analog in), se non presente input analogico, il booster 12v resta disattivato ed esclude il calcolo sulle forchette;
  - Le porta B è abilitata all'uso di SHT31 (i2c) o DS18B20 (One Wire) o pluviometro (digital in);
  - La porta A non è momentaneamente utilizzata
  - Alcuni sensori prevedono due formati diversi del dato da spedire, a seconda della trasmittente utilizzata e quindi dalla grandezza del pacchetto di byte da inviare

  Fabio Crivellaro 2024  :::
  UPDATE 2x Giugno     :::  aggiunto analogReference(EXTERNAL) in void setup()
  UPDATE 25 Giugno       :::  update del codice delle porte C e D, sensori umidità, pulizia variabili inutilizzate - aggiunte a inizio void loop()
*/

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SoftwareSerial.h>
#include "LowPower.h"
#include <avr/wdt.h>
#include <Wire.h>
#include "I2C_SCANNER.h"
#include "Adafruit_SHT31.h"
#include <EEPROM.h>
#include <BH1750.h>

/* alimetazione sensori */
#define BOOST_EN 3      // alimentazione 5/3.3 volt (D e C)
#define BOOST_SHTDWN 9  // booster 12v per determinati sensori analogici (D e C)
#define IO_ENABLE A0    // alimentazione (A e B)

/* pin */
#define AI_D A2  // pin 5
#define AI_C A1  // pin 5
word sensorValue_C;
word sensorValue_D;
#define DGTL_B 10  // pin 3 (centrale)
#define DGTL_C 11  // pin 3 (centrale)

/* pluviometro (B) */
unsigned long LS_count_End = 0;
bool LS_count_currentState = 1;
bool LS_count_previousState = 1;
int LS_count = 0;

/* anemometro (C) */
unsigned long HS_count_End = 0;
bool HS_count_currentState = 1;
bool HS_count_previousState = 1;
int HS_count = 0;


/* one wire (B)*/
#define ONEWIRE_PIN 10
OneWire oneWire1(ONEWIRE_PIN);
DallasTemperature sensors1(&oneWire1);

/* luxmetro (A) */
float lux_sum = 0;
int lux = 65534;


/* trasmittente */
#define rxpin 6
#define txpin 7
SoftwareSerial Serial_Radio(rxpin, txpin);
/*sigfox*/
#define SFOX_RST 2
#define SERIAL_SIGFOX 9600
//SoftwareSerial Sigfox =  SoftwareSerial(rxpin, txpin); // set up a new serial port
/*lora*/
#define SERIAL_LORA 1200

#define BUZZER 5

#define SERIAL_DEBUG 9600

/*i2c */
I2C_SCANNER scanner;
Adafruit_SHT31 sht31 = Adafruit_SHT31();
BH1750 lightMeter;
#define SWITCH_I2C 8


bool analog_connect = 1;
word hum1 = 65534;
word hum2 = 65534;
int hum_sum = 0;

float temp;
int temp1 = 65534;
float temp_sum = 0;
uint8_t tint;
uint8_t tfloat;

float hum;
uint8_t hum3 = 65534;
uint8_t hint;
uint8_t hfloat;

int vbat_meas;
uint8_t vbat;

uint8_t msgSF[12];

bool debug = 1;

char ID_NODE_1;
char ID_NODE_2;
char ID_NODE_3;
char ID_NODE_4;
char ID_NODE_5;
char ID_NODE_6;
byte newName[6];
bool nameOk;
bool nameChanged;
byte msgL[70];
bool i2c = 0;

int cycle_counter = 0;

//-------------PRENDE ID NUMB
String getID() {
  String id = "";
  char output;
  Serial_Radio.print("AT$I=10\r");
  while (!Serial_Radio.available()) {}

  while (Serial_Radio.available()) {
    output = Serial_Radio.read();
    id += output;
    delay(10);
  }
  if (debug) {
    Serial.print("Sigfox Device ID: ");
    Serial.println(id);
  }
  return id;
}

//--------------PRENDE PAC NUMB
String getPAC() {
  String pac = "";
  char output;
  Serial_Radio.print("AT$I=11\r");
  while (!Serial_Radio.available()) {}
  while (Serial_Radio.available()) {
    output = Serial_Radio.read();
    pac += output;
    delay(10);
  }
  if (debug) {
    Serial.print("PAC number: ");
    Serial.println(pac);
  }
  return pac;
}

// ----------------SPLITTA NUMERI FLOAT-----------------
void splitFloat(float number, uint8_t &integerPart, uint8_t &decimalPart) {
  // Estrai la parte intera
  integerPart = static_cast<int>(number);
  // Estrai la parte decimale
  decimalPart = static_cast<int>((number - integerPart) * 10);
}

//------------LETTURA BATTERIA senza PIN AN dedicato------
long readVcc() {
  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
  ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#elif defined(__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
  ADMUX = _BV(MUX5) | _BV(MUX0);
#elif defined(__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
  ADMUX = _BV(MUX3) | _BV(MUX2);
#else
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#endif
  delay(2);             // Wait for Vref to settle
  ADCSRA |= _BV(ADSC);  // Start conversion
  while (bit_is_set(ADCSRA, ADSC))
    ;                   // measuring
  uint8_t low = ADCL;   // must read ADCL first - it then locks ADCH
  uint8_t high = ADCH;  // unlocks both
  long result = (high << 8) | low;
  result = 1125300L / result;  // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  return result;               // Vcc in millivolts
}

//------------INVIA MESSAGGIO SF
void sendMessage(uint8_t msg[], int size) {
  Serial.println("Inside sendMessage");
  String status = "";
  String hexChar = "";
  String sigfoxCommand = "";
  char output;
  sigfoxCommand += "AT$SF=";
  for (int i = 0; i < size; i++) {
    hexChar = String(msg[i], HEX);
    //padding
    if (hexChar.length() == 1) {
      hexChar = "0" + hexChar;
    }
    sigfoxCommand += hexChar;
  }
  if (debug) {
    Serial.println("Sending...");
    Serial.println(sigfoxCommand);
  }
  Serial_Radio.println(sigfoxCommand);
  while (!Serial_Radio.available()) {
    wdt_reset();
    if (debug) Serial.println("Waiting for response");
    delay(1000);
  }
  while (Serial_Radio.available()) {
    output = (char)Serial_Radio.read();
    status += output;
    delay(10);
  }
  if (debug) {
    Serial.println();
    Serial.print("Status \t");
    Serial.println(status);
  }
}

void setup() {
  analogReference(EXTERNAL);
  Serial.begin(SERIAL_DEBUG);
  delay(100);

  //pin radio
  //pinMode(rxpin, INPUT);
  //pinMode(txpin, OUTPUT);

  ID_NODE_1 = char(EEPROM.read(0));
  ID_NODE_2 = char(EEPROM.read(1));
  ID_NODE_3 = char(EEPROM.read(2));
  ID_NODE_4 = char(EEPROM.read(3));
  ID_NODE_5 = char(EEPROM.read(4));
  ID_NODE_6 = char(EEPROM.read(5));
  if (ID_NODE_1 == 'S') {
    Serial.println("FW abilitato per Sigfox");
    Serial.print("Nome: ");
    Serial.print(ID_NODE_1);
    Serial.print(ID_NODE_2);
    Serial.print(ID_NODE_3);
    Serial.print(ID_NODE_4);
    Serial.print(ID_NODE_5);
    Serial.println(ID_NODE_6);
    for (int i = 0; i <= 1; i++) {  // 2 beep per SigFox
      delay(100);
      for (int i = 0; i <= 500; i++) {
        digitalWrite(BUZZER, HIGH);
        delayMicroseconds(200);
        digitalWrite(BUZZER, LOW);
        delayMicroseconds(200);
      }
    }
  } else if (ID_NODE_1 == 'L') {
    Serial.println("FW abilitato per Lora");
    Serial.print("Nome: ");
    Serial.print(ID_NODE_1);
    Serial.print(ID_NODE_2);
    Serial.print(ID_NODE_3);
    Serial.print(ID_NODE_4);
    Serial.print(ID_NODE_5);
    Serial.println(ID_NODE_6);
    /*for(int i=0; i<=5; i++){          // 6 beep per Lora
      delay (100);
      for(int i=0; i<=500; i++){
        digitalWrite (BUZZER, HIGH);
        delayMicroseconds(200);
        digitalWrite(BUZZER, LOW);
        delayMicroseconds(200);
      }
      }*/
  } else
    Serial.println("FW per uso con SigFox o Lora, inserisci nome.");

  Serial.println("Cambiare nome? (SFxxxx per SigFox - Lxxxxx per Lora)");
  for (int i = 0; i < 10; i++) {
    delay(1000);
  }

  if (Serial.available() == 0) {
    nameOk = (ID_NODE_1 == 'S' || ID_NODE_1 == 'L');
  } else if (Serial.available() == 7) {
    for (int i = 0; i < 6; i++) {
      newName[i] = Serial.read();
    }

    if (newName[0] == 83 || newName[0] == 76) {
      ID_NODE_1 = char(newName[0]);
      ID_NODE_2 = char(newName[1]);
      ID_NODE_3 = char(newName[2]);
      ID_NODE_4 = char(newName[3]);
      ID_NODE_5 = char(newName[4]);
      ID_NODE_6 = char(newName[5]);

      for (int i = 0; i < 6; i++) {
        EEPROM.write(i, newName[i]);
        delay(100);
      }

      Serial.print("Nuovo nome: ");
      Serial.print(ID_NODE_1);
      Serial.print(ID_NODE_1);
      Serial.print(ID_NODE_1);
      Serial.print(ID_NODE_1);
      Serial.print(ID_NODE_1);
      Serial.print(ID_NODE_1);

      nameOk = nameChanged = true;
    } else {
      Serial.println("Il formato del nome deve essere SFxxxx o Lxxxxx");
      delay(100);
    }
  } else {
    Serial.println("Il nuovo nome deve essere di 6 caratteri");
    delay(100);
  }

  Serial.println("");
  if (!nameOk) {
    Serial.println("Nome corrente non valido");
    Serial.println("");
    delay(200);
  }

  while (Serial.available() > 0) {
    Serial.read();
  }

  wdt_disable();
  wdt_enable(WDTO_8S);

  pinMode(BOOST_EN, OUTPUT);
  digitalWrite(BOOST_EN, HIGH);
  pinMode(BOOST_SHTDWN, OUTPUT);
  digitalWrite(BOOST_SHTDWN, LOW);
  pinMode(IO_ENABLE, OUTPUT);
  digitalWrite(IO_ENABLE, HIGH);
  pinMode(AI_D, INPUT);
  pinMode(AI_C, INPUT);
  pinMode(SWITCH_I2C, OUTPUT);

  Wire.begin();
  scanner.begin();

  /*if (! sht31.begin(0x44))
    Serial.println("Couldn't find SHT31");*/
  for (int addr = 0; addr < 128; addr++) {
    if (scanner.ping(addr))
      i2c = 1;
  }
  if (!i2c) {
    Wire.end();
    digitalWrite(IO_ENABLE, LOW);
  }

  if (!analogRead(AI_C) || !analogRead(AI_D)) {
    hum1 = 65534;
    hum2 = 65534;
    analog_connect = 0;
  }
  wdt_reset();
}
//-------------------------------------------- LOOP ----------------------------------------------------
void loop() {

  //////////////////////// PORTA C e D - NUOVO ////////////////////////
  delay(100);
  digitalWrite(AI_C, LOW);
  digitalWrite(AI_D, LOW);

  digitalWrite(BOOST_EN, HIGH); //apre la corrente per abilitare la lettura da parte dei senosri
  digitalWrite(BOOST_SHTDWN, HIGH);
  wdt_reset();

  delay(500);
  analogRead(AI_C);
  delay(10);
  analogRead(AI_D);
  delay(10);

  for (int i = 0; i < 30; i++) {
    sensorValue_C += analogRead(AI_C);
    sensorValue_D += analogRead(AI_D);
    delay(10);
  }
  sensorValue_C /= 30;
  sensorValue_D /= 30;
  if (sensorValue_C > 1000) sensorValue_C = 1000;
  if (sensorValue_D > 1000) sensorValue_D = 1000;

  if (debug) {
    Serial.println("Valore sensore porta C:");
    Serial.println(sensorValue_C);
    Serial.println("Valore sensore porta D:");
    Serial.println(sensorValue_D);
  }

  digitalWrite(BOOST_EN, LOW); // chiude la corrente aperta a inizio lettura
  digitalWrite(BOOST_SHTDWN, LOW);

  //>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> PORTA B <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
  digitalWrite(IO_ENABLE, HIGH);
  delay(200);  //ritardo necessario alla stabilizzazione della tensione sulle porte A e B (verificato)
  if (i2c) {
    digitalWrite(8, LOW);  // switch i2c A/B
    Wire.begin();
    //--------------- SHT31 TEMPERATURA (I2C) -------------------------------------//
    if (sht31.begin(0x44)) {  //controlla se sht31 è connesso
      delay(10);
      wdt_reset();
      temp = (sht31.readTemperature());
      delay(2);
      temp = (sht31.readTemperature());
      temp = 0;
      temp_sum = 0;
      for (int i = 0; i < 10; i++) {
        temp = (sht31.readTemperature());
        temp_sum += temp;
        delay(2);
      }
      temp = temp_sum / 10.00;
      if (debug) {
        if (!isnan(temp)) {
          Serial.print("temp i2c *C = ");
          Serial.println(temp);
        } else {
          Serial.println("Failed to read temperature");
        }
      }

      if (ID_NODE_1 == 'L') {
        temp += 30.00;               //traforma la temperatura in un numero sempre positivo
        temp1 = float(temp * 10.0);  //e in un numero intero
      }
      if (ID_NODE_1 == 'S') {
        splitFloat(temp, tint, tfloat);
        if (tfloat >= 8) {         // CASO .8 e .9
          tint += 1;               // sale di un grado
          tint += 30;              //mantenere valori non negativi
          tint *= 2;               //diventa un numero pari
        } else if (tfloat <= 2) {  // CASO .1 e .2
          tint += 30;              //mantenere valori non negativi
          tint *= 2;               //diventa un numero pari
        } else {                   // CASO .3, .4, .5, .6,.7
          tint += 30;              //mantenere valori non negativi
          tint *= 2;               //diventa un numero pari
          tint += 1;               //diventa un numero dispari
        }
        temp1 = tint;
      }

      //--------------- SHT31 UMIDITA (I2C) -------------------------------------//
      hum = (sht31.readHumidity());
      delay(1);
      hum = (sht31.readHumidity());
      hum = 0;
      hum_sum = 0;

      for (int i = 0; i < 10; i++) {
        hum = (sht31.readHumidity());
        hum_sum += hum;
        delay(2);
      }
      hum = hum_sum / 10;
      if (debug) {
        if (!isnan(hum)) {
          Serial.print("Hum. i2c % = ");
          Serial.println(hum);
        } else {
          Serial.println("Failed to read humidity");
        }
      }
      splitFloat(hum, hint, hfloat);
      if (hfloat >= 8) {         // CASO .8 e .9
        hint += 1;               // sale di un grado
        hint *= 2;               //diventa un numero pari
      } else if (tfloat <= 2) {  // CASO .1 e .2
        hint *= 2;               //diventa un numero pari
      } else {                   // CASO .3, .4, .5, .6,.7
        hint *= 2;               //diventa un numero pari
        hint += 1;               //diventa un numero dispari
      }

      hum3 = hint;

      wdt_reset();
      Wire.end();
    }
  }

  //--------------- ONE WIRE TEMPERATURA -------------------------------------//
  if (!i2c) {
    sensors1.requestTemperatures();
    temp = sensors1.getTempCByIndex(0);
    if (temp > -100) {  //controlla se oneWire è connesso
      delay(2);
      temp = sensors1.getTempCByIndex(0);
      temp = 0;
      temp_sum = 0;

      for (int i = 0; i < 10; i++) {
        temp = sensors1.getTempCByIndex(0);
        temp_sum += temp;
        delay(2);
      }
      temp = temp_sum / 10;  //media di 10 campioni per una misura piu accurata della temperatura

      if (debug) {
        if (!isnan(temp)) {
          Serial.print("temp oneWire *C = ");
          Serial.println(temp);
        } else {
          Serial.println("Failed to read temperature");
        }
      }

      if (ID_NODE_1 == 'L') {
        temp += 30.00;             //traforma la temperatura in un numero sempre positivo
        temp1 = int(temp * 10.0);  //e in un numero intero
      } else if (ID_NODE_1 == 'S') {
        splitFloat(temp, tint, tfloat);
        if (tfloat >= 8) {         // CASO .8 e .9
          tint += 1;               // sale di un grado
          tint += 30;              //mantenere valori non negativi
          tint *= 2;               //diventa un numero pari
        } else if (tfloat <= 2) {  // CASO .1 e .2
          tint += 30;              //mantenere valori non negativi
          tint *= 2;               //diventa un numero pari
        } else {                   // CASO .3, .4, .5, .6,.7
          tint += 30;              //mantenere valori non negativi
          tint *= 2;               //diventa un numero pari
          tint += 1;               //diventa un numero dispari
        }
        temp1 = tint;
      }
    } else {
      if (debug)
        Serial.println("Pluviometro connesso (misurazione di 10 secondi)...");
      pinMode(DGTL_B, INPUT);
      digitalWrite(DGTL_B, HIGH);
      delay(10);
      LS_count = 0;
      LS_count_End = millis() + 10000;  //90000;
      while (millis() < LS_count_End) {
        wdt_reset();
        if (digitalRead(DGTL_B) == HIGH)
          LS_count_currentState = 1;
        else
          LS_count_currentState = 0;
        if (LS_count_currentState != LS_count_previousState) {
          if (LS_count_currentState == 1)
            LS_count += 1;
        }
        LS_count_previousState = LS_count_currentState;
        delay(10);
      }
      if (LS_count <= 0)
        LS_count = 65534;
      digitalWrite(DGTL_B, LOW);
      if (debug) {
        Serial.print("PLUVIOMETRO numero di impulsi: ");
        Serial.println(LS_count);
      }
    }
  }

  //>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> PORTA A <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
  digitalWrite(SWITCH_I2C, HIGH);
  Wire.begin();
  delay(50);
  //--------------- LUXIMETRO (I2C) -------------------------------------//
  if (lightMeter.begin()) {
    delay(150);  // a 50-100 ms perde le prime misurazioni
    lux_sum = 0;
    Serial.print("LUXMETRO connesso: ");

    for (int i = 0; i < 10; i++) {
      lux_sum += lightMeter.readLightLevel();
      delay(10);
    }

    lux_sum /= 10;
    Serial.print(lux_sum);
    Serial.println(" lx");
  }
  if (ID_NODE_1 == 'L') {
    lux = int(lux * 10.0);  //e in un numero intero
  } else if (ID_NODE_1 == 'S') {
    splitFloat(lux, tint, tfloat);
    if (tfloat >= 80) {         // CASO .8 e .9
      tint += 1;                // sale di un grado
      tint *= 2;                //diventa un numero pari
    } else if (tfloat <= 20) {  // CASO .1 e .2
      tint *= 2;                //diventa un numero pari
    } else {                    // CASO .3, .4, .5, .6,.7
      tint *= 2;                //diventa un numero pari
      tint += 1;                //diventa un numero dispari
    }
    lux = tint;
  }
  digitalWrite(SWITCH_I2C, LOW);
  Wire.end();
  //>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> BATTERIA <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
  //---------------LETTURA BATTERIA--------------------------------------------------
  vbat_meas = readVcc();
  vbat = (vbat_meas - 2500) / 8;  // Compressione in un byte

  if (debug) {
    Serial.print("Tensione batteria: ");
    Serial.println(vbat);
  }
  digitalWrite(IO_ENABLE, LOW);

  //>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> TRASMISSIONE <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
  //--------------COSTRUISCE MESSAGGIO SF (MAX 12 BYTE)---------
  for (int i = 0; i < 12; ++i) {
    msgSF[i] = 0;  // Azzera Messaggio SFX
  }

  //-------------- MESSAGGIO SIGFOX (MAX 12 BYTE)---------
  msgSF[0] = 0xA1;
  msgSF[1] = 0;
  msgSF[2] = highByte(sensorValue_C);
  msgSF[3] = lowByte(sensorValue_C);
  msgSF[4] = highByte(sensorValue_D);
  msgSF[5] = lowByte(sensorValue_D);
  msgSF[6] = temp1;
  msgSF[7] = hum3;  // Umidità aria relativa (da versione 0) Sensore SHT
  msgSF[8] = 0;
  msgSF[9] = 0;
  msgSF[10] = vbat;
  msgSF[11] = 0xED;

  //-------------- MESSAGGIO LORA (MAX 70 BYTE)---------
  msgL[0] = ID_NODE_1;
  msgL[1] = ID_NODE_2;
  msgL[2] = ID_NODE_3;
  msgL[3] = ID_NODE_4;
  msgL[4] = ID_NODE_5;
  msgL[5] = ID_NODE_6;
  //----------------------------------- A1
  msgL[6] = highByte(lux);
  msgL[7] = lowByte(lux);
  //----------------------------------- A2
  msgL[8] = highByte(65534);
  msgL[9] = lowByte(65534);
  //---------------------------------- A3
  msgL[10] = highByte(65534);
  msgL[11] = lowByte(65534);
  //---------------------------------- A4
  msgL[12] = highByte(65534);
  msgL[13] = lowByte(65534);
  //---------------------------------- A5
  msgL[14] = highByte(65534);
  msgL[15] = lowByte(65534);
  //---------------------------------- A6
  msgL[16] = highByte(65534);
  msgL[17] = lowByte(65534);
  //---------------------------------- B7          (SHT31 temperatura aria)
  msgL[18] = highByte(temp1);
  msgL[19] = lowByte(temp1);
  //---------------------------------- B8          (SHT31 umidità aria)
  msgL[20] = highByte(hum3);
  msgL[21] = lowByte(hum3);
  //---------------------------------- B9
  msgL[22] = highByte(65534);
  msgL[23] = lowByte(65534);
  //---------------------------------- B10         (PLUVIOMETRO velocità vento)
  msgL[24] = highByte(LS_count);
  msgL[25] = lowByte(LS_count);
  //---------------------------------- B11
  msgL[26] = highByte(65534);
  msgL[27] = lowByte(65534);
  //---------------------------------- B12
  msgL[28] = highByte(65534);
  msgL[29] = lowByte(65534);
  //---------------------------------- C13         (FORCHETTA umidità terreno)
  msgL[30] = highByte(sensorValue_C);
  msgL[31] = lowByte(sensorValue_C);
  //---------------------------------- C14         (ANEMOMETRO millimetri pioggia)
  msgL[32] = highByte(HS_count);
  msgL[33] = lowByte(HS_count);
  //---------------------------------- C15
  msgL[34] = highByte(65534);
  msgL[35] = lowByte(65534);
  //---------------------------------- C16
  msgL[36] = highByte(65534);
  msgL[37] = lowByte(65534);
  //---------------------------------- C17
  msgL[38] = highByte(65534);
  msgL[39] = lowByte(65534);
  //---------------------------------- C18
  msgL[40] = highByte(65534);
  msgL[41] = lowByte(65534);
  //---------------------------------- C19
  msgL[42] = highByte(65534);
  msgL[43] = lowByte(65534);
  //---------------------------------- C20
  msgL[44] = highByte(65534);
  msgL[45] = lowByte(65534);
  //---------------------------------- C21
  msgL[46] = highByte(65534);
  msgL[47] = lowByte(65534);
  //---------------------------------- C22
  msgL[48] = highByte(65534);
  msgL[49] = lowByte(65534);
  //---------------------------------- D23         (FORCHETTA umidità terreno)
  msgL[50] = highByte(sensorValue_D);
  msgL[51] = lowByte(sensorValue_D);
  //---------------------------------- D24
  msgL[52] = highByte(65534);
  msgL[53] = lowByte(65534);
  //---------------------------------- D25
  msgL[54] = highByte(65534);
  msgL[55] = lowByte(65534);
  //---------------------------------- D26
  msgL[56] = highByte(65534);
  msgL[57] = lowByte(65534);
  //---------------------------------- D27
  msgL[58] = highByte(65534);
  msgL[59] = lowByte(65534);
  //---------------------------------- D28
  msgL[60] = highByte(65534);
  msgL[61] = lowByte(65534);
  //---------------------------------- D29           (CICLI del firmware dall'accensione)
  msgL[62] = highByte(cycle_counter);
  msgL[63] = lowByte(cycle_counter);
  //---------------------------------- BAT30         (BATTERIA)
  msgL[64] = highByte(vbat);
  msgL[65] = lowByte(vbat);
  //----------------------------------- RC
  msgL[66] = 50;  //sostituire con valore randomico
  //------------------------------ END
  msgL[67] = 0;
  msgL[68] = 255;
  msgL[69] = 255;

  /*Serial.println ("Stringa SigFox:");
    for(int i=0; i<12; i++){
    Serial.print ("BYTE ");
    Serial.print (i);
    Serial.print (" : ");
    Serial.println (msgSF[i]);
    }
    Serial.println ("Stringa Lora:");
    for(int i=0; i<70; i++){
    Serial.print ("BYTE ");
    Serial.print (i);
    Serial.print (" : ");
    Serial.println (msgL[i]);
    }*/

  wdt_reset();

  //--------------INVIA MESSAGGIO SF-----------------
  if (ID_NODE_1 == 'S') {
    pinMode(SFOX_RST, OUTPUT);
    digitalWrite(SFOX_RST, HIGH);
    Serial_Radio.begin(SERIAL_SIGFOX);
    delay(100);
    getID();
    delay(100);
    getPAC();
    delay(5000);
    wdt_reset();
    sendMessage(msgSF, 12);
    digitalWrite(SFOX_RST, LOW);
    for (int i = 0; i <= 100; i++) {
      digitalWrite(BUZZER, HIGH);
      delayMicroseconds(150);
      digitalWrite(BUZZER, LOW);
      delayMicroseconds(150);
    }  //--------------INVIA MESSAGGIO L-----------------
  } else if (ID_NODE_1 == 'L') {
    Serial_Radio.begin(SERIAL_LORA);
    delay(100);
    for (int j = 0; j < 70; j++) {
      Serial_Radio.write(msgL[j]);
    }
    for (int i = 0; i <= 100; i++) {
      digitalWrite(BUZZER, HIGH);
      delayMicroseconds(150);
      digitalWrite(BUZZER, LOW);
      delayMicroseconds(150);
    }
  }
  Serial_Radio.end();
  wdt_disable();

  for (int i = 0; i < 112; i++) {  //15minuti*60secondi=900secondi/8secondi=112 iterazioni
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  }
  wdt_enable(WDTO_8S);
  sensorValue_C = 0;
  sensorValue_D = 0;
  cycle_counter++;
}
//------------------------------------------ FINE LOOP --------------------------------------------------