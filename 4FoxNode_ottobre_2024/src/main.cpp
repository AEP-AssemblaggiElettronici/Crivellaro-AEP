#include <Arduino.h>
#include "defines.h"           // header con tutte le definizioni di pin, indirizzi dispositivi...
#include "forchetta_umidita.h" // sensore umidità
#include "luxmetro.h"          // luxmetro
#include "misura_peso.h"       // sensore peso
#include "anemometro.h"        // anemometro
#include "banderuola.h"        // sensore direzione vento
#include "pluviometro.h"       // pluviometro
#include "dallas.h"            // header per sensori dallas semiconductors
#include "sht3x.h"             // sensore I²C temperatura e umidità
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include "LowPower.h" // per entrare in modalità sleep alla fine di ogni ciclo
#include <avr/wdt.h>  // WOFF WOFF! watchdog
#include <main.h>
#include <SPI.h>
#include "Wire.h"

char dispositivoID[7];
long vbat_meas;                                      // misura batteria
uint8_t vbat;                                        // variabile batteria
unsigned long int contaCicli = 0;                    // contatore cicli programma
int sogliaTempo;                                     // definisce quanto tempo il dispositivo rimarrà in standby
unsigned long taraturaC;                             // taratura peso sulla porta C
unsigned long taraturaD;                             // taratura peso sulla porta D
SoftwareSerial Radio = SoftwareSerial(rxPin, txPin); // Definiamo la radio
bool taratura = 0;                                   // taratura sensori peso al primo ciclo
////////////////////////////////////////////////

void setup()
{
  ////////////////////////////////////////////////////////// Primo avvio resetta EEPROM
  if (EEPROM.read(64) != 10)
  {
    EEPROM.write(64, 10);
    EEPROM.write(0, 'S');
    for (int i = 1; i < 6; i++)
      EEPROM.write(i, '0');
  }
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  analogReference(EXTERNAL); // per evitare instabilità nella lettura dei sensori analogici

  Serial.begin(BAUD);

  for (int i = 0; i < 6; i++)
    dispositivoID[i] = EEPROM.read(i);

  ////////////////////////////////////////////////////////// Settaggio ID dispositivo ('S' per usare protrocollo SigFox, 'L' per utilizzare LORA)
  Serial.print("ID dispositivo in memoria: ");
  Serial.print((String)dispositivoID);
  Serial.println();
  Serial.println("Cambiare ID e protocollo dispositivo? (premere entro 5 secondi 's' o qualsiasi altro tasto per procedere)");
  unsigned int tempoEditDispositivo = millis();
  while (!Serial.available() && millis() - tempoEditDispositivo < 5000)
    ; // attende 5 secondi per la pressione del tasto 's'
  if (Serial.available() > 0)
  {
    if (Serial.read() == 's')
    {
      Serial.println("Inserire nuovo ID ('Lxxxxxx' per protocollo LORA, 'Sxxxxxx' per protocollo SigFox): ");
      int iID = 0;
      while (iID < 6)
      {
        while (!Serial.available())
          ; // attende che venga digitato un input
        dispositivoID[iID] = Serial.read();
        Serial.print(dispositivoID[iID]); // logga su seriale l'ID digitato in tempo reale
        iID++;
      }
    }
  }
  if (dispositivoID[0] != 'S' &&
      dispositivoID[0] != 's' &&
      dispositivoID[0] != 'L' &&
      dispositivoID[0] != 'l') // se il nome non è valido, RESET!
  {
    Serial.println("ID dispositivo non valido!");
    delay(1000);
    reboot();
  }
  dispositivoID[0] = toUpperCase(dispositivoID[0]);
  Serial.println();
  Serial.print("Protocollo utilizzato: ");
  Serial.print(dispositivoID[0] == 'S' ? "SigFox" : "LORA");
  Serial.println();
  Serial.print("ID dispositivo: ");
  for (int i = 0; i < 6; i++) // aggiornamento ID dispositivo
    EEPROM.update(i, dispositivoID[i]);
  for (int i = 0; i < 6; i++)
    Serial.print(dispositivoID[i]);
  Serial.println();
  delay(1000);
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  buzzer();
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  ////////////////////////////////////////////////////////// Setup generico...
  wdt_disable();
  wdt_enable(WDTO_8S);

  Wire.begin();
  delay(1000);
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
}

void loop()
{
  Radio = SoftwareSerial(rxPin, txPin); // Ridefiniamo gli I/O della radio a ogni inizio ciclo, dopo la modalità a risparmio energetico
  // pin trasmissione
  pinMode(rxPin, INPUT);
  pinMode(txPin, OUTPUT);
  // pin step up
  pinMode(BOOST_EN, OUTPUT);
  digitalWrite(BOOST_EN, 1); // tiro su il pin che alimenta le porte C e D
  pinMode(BOOST_SHTDWN, OUTPUT);
  digitalWrite(BOOST_SHTDWN, 0); //
  pinMode(IO_ENABLE, OUTPUT);
  digitalWrite(IO_ENABLE, 1); // tiro su il pin che alimenta le porte A e B
  pinMode(I2C_SWITCH, OUTPUT);

  ////////////////////////////////////////////////////////// Lettura batteria
  vbat_meas = readVcc();
  vbat = (vbat_meas - 2500) / 8; // Compressione in un byte
#if DEBUG
  Serial.print("Tensione batteria: ");
  Serial.println(vbat);
#endif
  digitalWrite(IO_ENABLE, 0);
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////// Raccolta letture sensori
  //////////////////////////////////////////////////////////

  wdt_reset();
  Serial.println("Inizio lettura sensori...");
  digitalWrite(IO_ENABLE, 1);
  delay(300);

  if (!taratura) // Taratura sensori peso
  {
    Serial.println("Taratura sensori peso su porte C e D");
    digitalWrite(I2C_SWITCH, 1);
    delay(300);
    taraturaC = misura_peso(PORT_C_J_1_4, PORT_C_J_1_5, 0);
    taraturaD = misura_peso(PORT_D_J_4_4, PORT_D_J_4_5, 0);
    taratura = 1;
    digitalWrite(I2C_SWITCH, 0);
  }

  // A
#if DEBUG
  Serial.println("Porta A");
#endif
  digitalWrite(I2C_SWITCH, 1);
  word dallas1 = dallasRead(PORT_A);
  unsigned short int *sht3x_data = sht3x(SHT3X);
  word sht31_1Temp = sht3x_data[0];
  word sht31_1Hum = sht3x_data[1];
  word lux1 = luxmetro(LUXMETRO1);
// B
#if DEBUG
  Serial.println("Porta B");
#endif
  digitalWrite(I2C_SWITCH, 0);
  word dallas2 = dallasRead(PORT_B);
  sht3x_data = sht3x(SHT3X);
  word sht31_2Temp = sht3x_data[0];
  word sht31_2Hum = sht3x_data[1];
  word lux2 = luxmetro(LUXMETRO2);
  word pluv = pluviometro();
  word dren = 0xFFFE; // COMING SOON
// C
#if DEBUG
  Serial.println("Porta C");
#endif
  word dallas3 = dallasRead(PORT_C_J_1_5);
  word humC = forchetta_umidita(PORT_C_J_1_5);
  int peso1 = misura_peso(PORT_C_J_1_4, PORT_C_J_1_5, taraturaC);
  word anem = anemometro();
// D
#if DEBUG
  Serial.println("Porta D");
#endif
  word dallas4 = dallasRead(PORT_D_J_4_5);
  word humD = forchetta_umidita(PORT_D_J_4_5);
  int peso2 = misura_peso(PORT_D_J_4_4, PORT_D_J_4_5, taraturaD);
  word direzioneVento = banderuola();

  Serial.println("Fine lettura sensori");
  wdt_reset();
  delay(1000);
  digitalWrite(IO_ENABLE, 0);
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////// Trasmissione
  //////////////////////////////////////////////////////////

  if (dispositivoID[0] == 'S')
  {
//-------------- MESSAGGIO SIGFOX (MAX 12 BYTE)---------
#if DEBUG
    Serial.println("Preparazione ed invio messaggio SigFox");
#endif
    uint8_t msgSF[12];
    msgSF[0] = 0xA1;
    msgSF[1] = 0;
    msgSF[2] = highByte(humC);
    msgSF[3] = lowByte(humC);
    msgSF[4] = highByte(humD);
    msgSF[5] = lowByte(humD);
    msgSF[6] = sht31_2Temp;
    msgSF[7] = sht31_2Hum;
    msgSF[8] = 0xFE;
    msgSF[9] = 0xFE;
    msgSF[10] = vbat;
    msgSF[11] = 0xED;

    pinMode(SFOX_RST, OUTPUT);
    digitalWrite(SFOX_RST, 1);
    Radio.begin(SERIAL_SIGFOX);
    delay(100);
    getID();
    delay(100);
    getPAC();
    delay(5000);
    wdt_reset();
    sendMessageSF(msgSF, 12);
    digitalWrite(SFOX_RST, 0);

    Serial.println("Messaggio SigFox inviato");
#if DEBUG
    for (int i = 0; i < int(sizeof(msgSF) / sizeof(msgSF[0])); i++)
    {
      Serial.print(msgSF[i], HEX);
      Serial.print("|");
    }
    Serial.println();
#endif
  }
  else
  {
    //-------------- MESSAGGIO LORA (MAX 70 BYTE)---------
#if DEBUG
    Serial.println("Preparazione ed invio messaggio LORA");
#endif
    byte msgL[70];
    // I primi 6 bytes contengono i caratteri dell'ID LORA
    for (int i = 0; i < 6; i++)
      msgL[i] = dispositivoID[i];
    //---------------------------------- A01         (SHT31 Temperatura 1)
    msgL[6] = highByte(sht31_1Temp);
    msgL[7] = lowByte(sht31_1Temp);
    //---------------------------------- A02         (SHT31 Umidità 1)
    msgL[8] = highByte(sht31_1Hum);
    msgL[9] = lowByte(sht31_1Hum);
    //---------------------------------- A03         (Luxmetro 1)
    msgL[10] = highByte(lux1);
    msgL[11] = lowByte(lux1);
    //---------------------------------- A04         (SENSORE DALLAS 1)
    msgL[12] = highByte(dallas1);
    msgL[13] = lowByte(dallas1);
    //---------------------------------- A05
    msgL[14] = 0xFF;
    msgL[15] = 0xFE;
    //---------------------------------- A06
    msgL[16] = 0xFF;
    msgL[17] = 0xFE;
    //---------------------------------- B07         (SHT31 Temperatura 2)
    msgL[18] = highByte(sht31_2Temp);
    msgL[19] = lowByte(sht31_2Temp);
    //---------------------------------- B08         (SHT31 Umidità 2)
    msgL[20] = highByte(sht31_2Hum);
    msgL[21] = lowByte(sht31_2Hum);
    //---------------------------------- B09         (Luxmetro 2)
    msgL[22] = highByte(lux2);
    msgL[23] = lowByte(lux2);
    //---------------------------------- B10         (SENSORE DALLAS 2)
    msgL[24] = highByte(dallas2);
    msgL[25] = lowByte(dallas2);
    //---------------------------------- B11         (Pluviometro)
    msgL[26] = highByte(pluv);
    msgL[27] = lowByte(pluv);
    //---------------------------------- B12         (Drenato)
    msgL[28] = highByte(dren);
    msgL[29] = lowByte(dren);
    //---------------------------------- C13         (Forchetta umidità 1)
    msgL[30] = highByte(humC);
    msgL[31] = lowByte(humC);
    //---------------------------------- C14         (SENSORE DALLAS 3)
    msgL[32] = highByte(dallas3);
    msgL[33] = lowByte(dallas3);
    //---------------------------------- C15         (Anemometro)
    msgL[34] = highByte(anem);
    msgL[35] = lowByte(anem);
    //---------------------------------- C16
    msgL[36] = 0xFF;
    msgL[37] = 0xFE;
    //---------------------------------- C17
    msgL[38] = 0xFF;
    msgL[39] = 0xFE;
    //---------------------------------- C18
    msgL[40] = 0xFF;
    msgL[41] = 0xFE;
    //---------------------------------- C19
    msgL[42] = 0xFF;
    msgL[43] = 0xFE;
    //---------------------------------- C20         (Sensore peso 1)
    msgL[44] = highByte(peso1);
    msgL[45] = lowByte(peso1);
    //---------------------------------- C21
    msgL[46] = 0xFF;
    msgL[47] = 0xFE;
    //---------------------------------- C22
    msgL[48] = 0xFF;
    msgL[49] = 0xFE;
    //---------------------------------- D23         (FORCHETTA umidità 2)
    msgL[50] = highByte(humD);
    msgL[51] = lowByte(humD);
    //---------------------------------- D24         (SENSORE DALLAS 4)
    msgL[52] = highByte(dallas4);
    msgL[53] = lowByte(dallas4);
    //---------------------------------- D25         (Segnavento)
    msgL[54] = highByte(direzioneVento);
    msgL[55] = lowByte(direzioneVento);
    //---------------------------------- D26         (Sensore peso 2)
    msgL[56] = highByte(peso2);
    msgL[57] = lowByte(peso2);
    //---------------------------------- D27
    msgL[58] = 0xFF;
    msgL[59] = 0xFE;
    //---------------------------------- D28
    msgL[60] = 0xFF;
    msgL[61] = 0xFE;
    //---------------------------------- D29           (CICLI del firmware dall'accensione)
    msgL[62] = highByte(contaCicli);
    msgL[63] = lowByte(contaCicli);
    //---------------------------------- BAT30         (BATTERIA)
    msgL[64] = highByte(vbat);
    msgL[65] = lowByte(vbat);
    //----------------------------------- RC
    msgL[66] = 50; // sostituire con valore randomico
    //------------------------------ END
    msgL[67] = 0;
    msgL[68] = 255;
    msgL[69] = 255;

    Radio.begin(SERIAL_LORA);
    delay(100);
    for (int j = 0; j < 70; j++)
      Radio.write(msgL[j]);

    Serial.println("Messaggio LORA inviato");
#if DEBUG
    for (int i = 0; i < int(sizeof(msgL) / sizeof(msgL[0])); i++)
    {
      Serial.print(msgL[i], HEX);
      Serial.print("|");
    }
    Serial.println();
#endif
  }
  delay(3000);
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  ////////////////////////////////////////////////////////// Modalità risparmio energetico
  // spegnamo tutti i pin ed entriamo in un ciclo a basso consumo di durata variabile a seconda del protocollo usato
  pinMode(SFOX_RST, 0);
  pinMode(SFOX_RST, INPUT);
  digitalWrite(BOOST_EN, 0);
  pinMode(BOOST_EN, INPUT);
  digitalWrite(PORT_D_J_4_4, 0);
  pinMode(PORT_D_J_4_4, INPUT);
  digitalWrite(rxPin, 0);
  pinMode(rxPin, INPUT);
  digitalWrite(txPin, 0);
  pinMode(txPin, INPUT);
  digitalWrite(I2C_SWITCH, 0);
  pinMode(I2C_SWITCH, INPUT);
  digitalWrite(BOOST_SHTDWN, 0);
  pinMode(BOOST_SHTDWN, INPUT);
  digitalWrite(PORT_B, 0);
  pinMode(PORT_B, INPUT);
  digitalWrite(PORT_C_J_1_3, 0);
  pinMode(PORT_C_J_1_3, INPUT);
  digitalWrite(PORT_A, 0);
  pinMode(PORT_A, INPUT);
  digitalWrite(PORT_C_J_1_4, 0);
  pinMode(PORT_C_J_1_4, INPUT);
  digitalWrite(IO_ENABLE, 0);
  pinMode(IO_ENABLE, INPUT);
  digitalWrite(PORT_C_J_1_5, 1);
  pinMode(PORT_C_J_1_5, INPUT); // SLK_D A1
  digitalWrite(PORT_D_J_4_5, 1);
  pinMode(PORT_D_J_4_5, INPUT); // SLK_C A2
  digitalWrite(PORT_D_J_4_3, 0);
  pinMode(PORT_D_J_4_3, INPUT);
  digitalWrite(SDA_PIN, 1); // pinMode(A4, INPUT);//PER NON FAR CONSUMARE I BMP IN SLEEP
  digitalWrite(SCL_PIN, 1); // pinMode(A5, INPUT);//PER NON FAR CONSUMARE I BMP IN SLEEP

  sogliaTempo = dispositivoID[0] == 'S' ? 113 : 38; //  se usiamo SigFox, la soglia è di un quarto d'ora, con LORA, 5 minuti (valori espressi in secondi)
#if DEBUG
  Serial.println(dispositivoID[0] == 'S' ? "Attesa... (15 minuti per SigFox)" : "Attesa... (5 minuti per LORA)");
#endif
  delay(1000);
  for (int i = 0; i < sogliaTempo; i++)
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);

  // 15 minuti * 60 secondi = 900 secondi / 8 secondi = 112.5 iterazioni (verrà arrotondato)
  // 5 minuti * 60 secondi = 300 secondi / 8 secondi = 37.5 iterazioni (verrà arrotondato)

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  delay(1000);
  contaCicli++;
  buzzer();
  delay(250);
  buzzer();
#if DEBUG
  Serial.println("Riavvio ciclo");
#endif
}

//------------- SigFOX: PRENDE ID NUMB
String getID()
{
  String id = "";
  char output;

  Radio.print("AT$I=10\r");
  while (!Radio.available())
    ;

  while (Radio.available())
  {
    output = Radio.read();
    id += output;
    delay(10);
  }

  Serial.print("Sigfox Device ID: ");
  Serial.println(id);

  return id;
}

//-------------- SigFOX: PRENDE PAC NUMB
String getPAC()
{
  String pac = "";
  char output;

  Radio.print("AT$I=11\r");

  while (!Radio.available())
    ;

  while (Radio.available())
  {
    output = Radio.read();
    pac += output;
    delay(10);
  }

  Serial.print("PAC number: ");
  Serial.println(pac);

  return pac;
}

//-------------- SigFOX: FUNZIONE PER INVIARE MESSAGGI
void sendMessageSF(uint8_t msg[], int size)
{
  Serial.println("Inside sendMessage");
  String status = "";
  String hexChar = "";
  String sigfoxCommand = "";
  char output;
  sigfoxCommand += "AT$SF=";
  for (int i = 0; i < size; i++)
  {
    hexChar = String(msg[i], HEX);
    // padding
    if (hexChar.length() == 1)
    {
      hexChar = "0" + hexChar;
    }
    sigfoxCommand += hexChar;
  }
  Serial.println("Sending...");
  Serial.println(sigfoxCommand);
  Radio.println(sigfoxCommand);
  while (!Radio.available())
  {
    wdt_reset();
    Serial.println("Waiting for response");
    delay(1000);
  }
  while (Radio.available())
  {
    output = (char)Radio.read();
    status += output;
    delay(10);
  }
  Serial.println();
  Serial.print("Status \t");
  Serial.println(status);
}

//------------LETTURA BATTERIA senza PIN AN dedicato------
long readVcc()
{
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
  delay(2);            // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA, ADSC))
    ;                  // measuring
  uint8_t low = ADCL;  // must read ADCL first - it then locks ADCH
  uint8_t high = ADCH; // unlocks both
  long result = (high << 8) | low;
  result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  return result;              // Vcc in millivolts
}

// BUZZER
void buzzer()
{
  for (int i = 0; i <= 2; i++)
  {
    delay(100);
    for (int i = 0; i <= 500; i++)
    {
      digitalWrite(BUZZER, 1);
      delayMicroseconds(200);
      digitalWrite(BUZZER, 0);
      delayMicroseconds(200);
    }
  }
}