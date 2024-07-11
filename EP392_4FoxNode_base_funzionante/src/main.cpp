/* ARDUINO UNO

> Il messaggio può avere una dimensione massima di 12 byte, è stato rappresentato tramite un'array di 12 interi unsigned
da 1 byte (8 bit) ciascuno. 
  - Il primo byte è stato riservato al riconoscimento della scheda in uso, è stata sfruttata la notazione esadecimale per associare alla scheda una sigla riconoscitiva 
    alfa-numerica.
  - Il secondo byte è vuoto, è un gap che precede i dati riservati alla sensoristica 
  - Dal terzo al decimo byte sono raccolti i dati ottenuti dalla sensoristica
  - L'undicesimo byte è dedicato allo stato di carica della batteria
  - Il dodicesimo byte è una sigla (ED) di chiusura dell'array.

> FORCHETTA C e D: il valore ricevuto dal sensore è stato splittato tra lowbyte e highbyte, per non perdere in precisione. In ricezione dovrà poi essere ricomposto in una word 
  (2 byte) per ottenere il valore di umidità corretto. Valori superiori a 1000 non verranno considerati in quanto già indice di allagamento (min 0, max 1000).

> I2C TEMP: il valore ricevuto dal sensore è in virgola mobile e può assumere valori negativi. Sommando 30 a questo valore avremmo una temperatura sempre positiva 
  compresa nel range 10 - 90 gradi. La parte decimale della temperatura è stata approssimata mantenendo una precisione di mezzo grado: moltiplicando per 2 otteniamo un 
  numero sempre pari, decidiamo poi se renderlo dispari sommando 1, oppure mantenerlo pari senza sommare 1. In ricezione, il valore andrà diviso per due e decrementato
  di 30 afinchè si ottenga il corretto valore di temperatura;

> I2C HUM: stesso ragionamento precedente, ma senza l'aggiunta di +30 a causa dell'assenza di valori negativi di umidità;

> BATTERIA: il valore ricevuto è la tensione della batteria in millivolt. Sottraendo 2500 e dividendo per 8 questo valore, otteniamo una perdita di precisione 
  trascurabile a vantaggio di un messaggio compresso in 8 bit. In ricezione andrà ricomposto il valore inviato, moltiplicandolo per 8 ed incrementandolo di 2500.
*/

#include <Arduino.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include "LowPower.h"
#include <avr/wdt.h>
#include <Wire.h>
#include "Adafruit_SHT31.h"

/* alimetazione sensori */
#define BOOST_EN 3 //alimentazione sulle porte D e C
#define BOOST_SHTDWN 9 //12v per alimentazione sensoristica analogica (D e C)
#define IO_ENABLE A0 //alimentazione sulle porte A e B

/* analog input */
#define AI_D A2
#define AI_C A1

/*sigfox*/
#define rxPin 6
#define txPin 7
#define SFOX_RST 2

//#define LED_sfx 13
//#define RADIO_PWR 6 

#define BUZZER 5

#define SERIAL_DEBUG 9600
#define SERIAL_SIGFOX 9600

//#define SWITCH_I2C 8

SoftwareSerial Sigfox = SoftwareSerial(rxPin, txPin); // set up a new serial port
Adafruit_SHT31 sht31 = Adafruit_SHT31();

char comando;

int umidita;
int umidita1 = 0;
int umidita2 = 0;

float temp;
uint8_t tint;
uint8_t tfloat;
float hum;
uint8_t hint;
uint8_t hfloat;

uint8_t hum_i2c_1;

int vbat_meas;
uint8_t vbat;

uint8_t msg[12];

int debug = 1;

// Prototipi funzioni
String getID();
String getPAC();
void splitFloat(float, uint8_t & , uint8_t & );
long readVcc();
void sendMessage(uint8_t[], int);

void setup() {
  Serial.begin(SERIAL_DEBUG);
  Serial.println("Premi \"A\" entro 5 secondi per attivare la seriale");
  analogReference(EXTERNAL);
  unsigned long startTime = millis();
  while (!Serial.available() && millis() - startTime < 5000) {}
  if (Serial.available() > 0) {
    comando = Serial.read();
    if (comando == 'A') {
      Serial.println("Comunicazione seriale attivata");
    } else {
      Serial.println("Comunicazione seriale disattivata");
      Serial.end();
    }
  } else {
    Serial.println("Comunicazione seriale disattivata");
    Serial.end();
  }

  // buzzer in accensione
  for (int i = 0; i <= 2; i++) {
    delay(100);
    for (int i = 0; i <= 500; i++) {
      digitalWrite(BUZZER, HIGH);
      delayMicroseconds(200);
      digitalWrite(BUZZER, LOW);
      delayMicroseconds(200);
    }
  }

  wdt_disable();
  wdt_enable(WDTO_8S);

  //pin sigfox
  pinMode(rxPin, INPUT);
  pinMode(txPin, OUTPUT);

  //pinMode(RADIO_PWR, OUTPUT); 
  pinMode(SFOX_RST, OUTPUT);
  digitalWrite(SFOX_RST, HIGH);
  //digitalWrite(RADIO_PWR, LOW);

  //pin step up
  pinMode(BOOST_EN, OUTPUT);
  pinMode(BOOST_SHTDWN, OUTPUT);
  pinMode(IO_ENABLE, OUTPUT);

  //pin sensori
  pinMode(AI_D, INPUT);
  pinMode(AI_C, INPUT);

  Sigfox.begin(SERIAL_SIGFOX);

  delay(100);
  getID();
  delay(100);
  getPAC();

} /////////////////////////////////////////////////FINE SETUP////////////////////////////////////////////////////////

void loop() {
  if (comando == 'A')
    Serial.begin(SERIAL_DEBUG);

  Serial.println("Inizio loop!");

  //---------------FORCHETTA UMIDITA' 1-------------
  digitalWrite(BOOST_EN, HIGH);
  digitalWrite(BOOST_SHTDWN, HIGH);
  digitalWrite(IO_ENABLE, HIGH);
  delay(5000);
  wdt_reset();

  //sensore umidità D
  umidita = analogRead(AI_D);
  umidita = 0;

  for (int i = 0; i < 10; i++) {
    umidita = analogRead(AI_D);
    umidita1 += umidita;
    delay(100);
  }

  umidita1 = umidita1 / 10;
  if (umidita1 >= 1000)
    umidita1 = 1000;

  wdt_reset();

  Serial.print("Il valore di umidità del sensore D è: ");
  Serial.println(umidita1);
  Serial.print("Il valore di umidità highByte del sensore D è: ");
  Serial.println(highByte(umidita1));
  Serial.print("Il valore di umidità lowByte del sensore D è: ");
  Serial.println(lowByte(umidita1));
  Serial.print("Il valore di umidità ricostruito del sensore D è: ");
  Serial.println(word(highByte(umidita1), lowByte(umidita1)));

  //---------------FORCHETTA UMIDITA' 2-------------
  umidita = analogRead(AI_C);
  umidita = 0;

  for (int i = 0; i < 10; i++) {
    umidita = analogRead(AI_C);
    umidita2 += umidita;
    delay(100);
  }
  digitalWrite(BOOST_EN, LOW);
  digitalWrite(BOOST_SHTDWN, LOW);

  umidita2 = umidita2 / 10;
  if (umidita2 >= 1000)
    umidita2 = 1000;

  wdt_reset();

  Serial.print("Il valore di umidità del sensore C è: ");
  Serial.println(umidita2);
  Serial.print("Il valore di umidità highByte del sensore C è: ");
  Serial.println(highByte(umidita2));
  Serial.print("Il valore di umidità lowByte del sensore C è: ");
  Serial.println(lowByte(umidita2));
  Serial.print("Il valore di umidità ricostruito del sensore C è: ");
  Serial.println(word(highByte(umidita2), lowByte(umidita2)));
  wdt_reset();

  //--------------- SHT31 TEMP -------------------------------------//
  //pinMode(8, OUTPUT);    
  //digitalWrite (8, LOW);        // switch i2c A/B
  if (!sht31.begin(0x44))
    Serial.println("Couldn't find SHT31");

  wdt_reset();

  temp = (sht31.readTemperature());

  splitFloat(temp, tint, tfloat);

  if (tfloat >= 8) { // CASO .8 e .9
    tint += 1; // sale di un grado
    tint += 30; //mantenere valori non negativi
    tint *= 2; //diventa un numero pari
  } else if (tfloat <= 2) { // CASO .1 e .2
    tint += 30; //mantenere valori non negativi
    tint *= 2; //diventa un numero pari 
  } else { // CASO .3, .4, .5, .6,.7
    tint += 30; //mantenere valori non negativi
    tint *= 2; //diventa un numero pari 
    tint += 1; //diventa un numero dispari
  }

  uint8_t temp_i2c_1 = tint;

  if (!isnan(temp)) {
    Serial.print("Temp *C = ");
    Serial.println(temp);
    Serial.print("il valore Temp *C da spedire è= ");
    Serial.println(temp_i2c_1);
  } else {
    Serial.println("Failed to read temperature");
  }

  //--------------- SHT31 HUM -------------------------------------//
  hum = (sht31.readHumidity());

  splitFloat(hum, hint, hfloat);

  if (hfloat >= 8) { // CASO .8 e .9
    hint += 1; // sale di un grado
    hint *= 2; //diventa un numero pari
  } else if (tfloat <= 2) { // CASO .1 e .2
    hint *= 2; //diventa un numero pari 
  } else { // CASO .3, .4, .5, .6,.7
    hint *= 2; //diventa un numero pari 
    hint += 1; //diventa un numero dispari
  }

  if (hint >= 1000)
    hum_i2c_1 = 1000;
  else
    hum_i2c_1 = hint;

  if (!isnan(hum)) {
    Serial.print("Hum. % = ");
    Serial.println(hum);
    Serial.print("il valore Hum. % da spedire è = ");
    Serial.println(hum_i2c_1);
  } else {
    Serial.println("Failed to read humidity");
  }

  wdt_reset();
  delay(1000);
  digitalWrite(IO_ENABLE, LOW);

  //---------------LETTURA BATTERIA--------------------------------------------------
  vbat_meas = readVcc();
  vbat = (vbat_meas - 2500) / 8; // Compressione in un byte

  Serial.print("La tensione della batteria è: ");
  Serial.println(vbat_meas);
  Serial.print("La tensione della batteria in formato compresso è: ");
  Serial.println(vbat);

  //--------------COSTRUISCE MESSAGGIO (MAX 12 BYTE)---------

  for (int i = 0; i < 12; ++i) {
    msg[i] = 0;
  } // Azzera Messaggio SFX

  msg[0] = 0xA1;
  msg[1] = 0;
  msg[2] = highByte(umidita1);
  msg[3] = lowByte(umidita1);
  msg[4] = highByte(umidita2);
  msg[5] = lowByte(umidita2);
  msg[6] = temp_i2c_1;
  msg[7] = hum_i2c_1;
  msg[8] = 0;
  msg[9] = 0;
  msg[10] = vbat;
  msg[11] = 0xED;

  for (int i = 0; i < 12; i++) {
    Serial.print("BYTE ");
    Serial.print(i);
    Serial.print(" : ");
    Serial.println(msg[i]);
  }

  wdt_reset();

  //--------------INVIA MESSAGGIO-----------------
  Sigfox.begin(SERIAL_SIGFOX);
  digitalWrite(SFOX_RST, HIGH);
  delay(5000);
  wdt_reset();
  sendMessage(msg, 12);
  digitalWrite(SFOX_RST, LOW);
  Sigfox.end();

  for (int i = 0; i <= 500; i++) {
    digitalWrite(BUZZER, HIGH);
    delayMicroseconds(150);
    digitalWrite(BUZZER, LOW);
    delayMicroseconds(150);
  }

  if (comando == 'A')
    Serial.end();
  wdt_disable();

    for (int i = 0; i < 112 ; i++) { //15minuti*60secondi=900secondi/8secondi=112 iterazioni
      LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    }

  wdt_enable(WDTO_8S);
}
///////////////////////////////////////////////FINE LOOP//////////////////////////////////////////////////////////////////

//-------------PRENDE ID NUMB
String getID() {
  String id = "";
  char output;

  Sigfox.print("AT$I=10\r");
  while (!Sigfox.available()) {}

  while (Sigfox.available()) {
    output = Sigfox.read();
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

  Sigfox.print("AT$I=11\r");

  while (!Sigfox.available()) {}

  while (Sigfox.available()) {
    output = Sigfox.read();
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
void splitFloat(float number, uint8_t & integerPart, uint8_t & decimalPart) {
  // Estrai la parte intera
  integerPart = static_cast < int > (number);
  // Estrai la parte decimale
  decimalPart = static_cast < int > ((number - integerPart) * 10);
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

  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA, ADSC)); // measuring

  uint8_t low = ADCL; // must read ADCL first - it then locks ADCH  
  uint8_t high = ADCH; // unlocks both

  long result = (high << 8) | low;

  result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  return result; // Vcc in millivolts
}

//------------INVIA MESSAGGIO
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

  Serial.println("Sending...");
  Serial.println(sigfoxCommand);
  Sigfox.println(sigfoxCommand);
  while (!Sigfox.available()) {
    wdt_reset();
    Serial.println("Waiting for response");
    delay(1000);

  }

  while (Sigfox.available()) {
    output = (char) Sigfox.read();
    status += output;
    delay(10);

  }

  Serial.println();
  Serial.print("Status \t");
  Serial.println(status);
}