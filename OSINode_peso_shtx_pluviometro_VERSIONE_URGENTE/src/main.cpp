/*
  Questo è il firmware per CARNEMOLLA, invia il messaggio LORA ogni 3 MINUTI
  il pluviometro all'interno del messaggio inviato ogni 3 minuti è solamente il dato di inizio ciclo
 */

#include <Arduino.h>
#include "defines.h"     // header con tutte le definizioni di pin, indirizzi dispositivi...
#include "misura_peso.h" // sensore peso
#include "sht3x.h"       // sensore I²C temperatura e umidità
#include "pluviometro.h" // pluviometro
#include <EEPROM.h>
#include <SoftwareSerial.h>
// #include "LowPower.h" // per entrare in modalità sleep alla fine di ogni ciclo
#include <avr/wdt.h> // WOFF WOFF! watchdog
#include <main.h>
#include <SPI.h>
#include "Wire.h"

char dispositivoID[7];
long vbat_meas;                   // misura batteria
uint8_t vbat;                     // variabile batteria
unsigned long int contaCicli = 0; // contatore cicli programma
unsigned long int sogliaTempo;    // definisce quanto tempo il dispositivo rimarrà in standby
unsigned long taraturaC;          // taratura peso sulla porta C
unsigned long taraturaD;          // taratura peso sulla porta D
unsigned long pesoPrecedente1 = 0;
unsigned long pesoPrecedente2 = 0;
SoftwareSerial Radio = SoftwareSerial(rxPin, txPin); // Definiamo la radio
bool taratura = 0;                                   // taratura sensori peso al primo ciclo
unsigned long int tempoAttuale;                      // variabile per salvare i millis()
unsigned long int tempoTrascorso = 0;                // tempo che è passato dal precedente intervallo
unsigned long int tempoTrascorso1ora = 0;            // tempo trascorso da 1 ora, per inviare il drenato
unsigned long int pluvio = 0;                        // conteggio pluviometro
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
    wdt_reset();
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
  wdt_reset();
  delay(1000);
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  wdt_disable();
  wdt_enable(WDTO_8S);

  sogliaTempo = dispositivoID[0] == 'S' ? TEMPO_SIGFOX : TEMPO_LORA;
  Wire.begin();
  Serial.end();
  wdt_reset();
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
  digitalWrite(BOOST_SHTDWN, 0); // se è a 0 tira fuori 4v, a 1 ne tira fuori 12v
  pinMode(IO_ENABLE, OUTPUT);
  digitalWrite(IO_ENABLE, 1); // tiro su il pin che alimenta le porte A e B
  pinMode(I2C_SWITCH, OUTPUT);

  ////////////////////////////////////////////////////////// Lettura batteria
  vbat_meas = readVcc();
  vbat = (vbat_meas - 2500) / 8; // Compressione in un byte
#if DEBUG
  Serial.begin(BAUD);
  Serial.print("Tensione batteria: ");
  Serial.println(vbat);
#endif
  digitalWrite(IO_ENABLE, 0);
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  if (!taratura) // Taratura sensori peso
  {
#if DEBUG
    Serial.println("Taratura sensori peso su porta C:");
#endif
    taraturaC = pesa(PORT_C_J_1_4, PORT_C_J_1_5, 0);
#if DEBUG
    Serial.println("Taratura sensori peso su porta D:");
#endif
    taraturaD = pesa(PORT_D_J_4_4, PORT_D_J_4_5, 0);
    taratura = 1;
    wdt_reset();
    buzzer(); // tre bip per notificare l'inizio della lettura dei sensori
    delay(100);
    buzzer();
    delay(100);
    buzzer();
  }

  digitalWrite(IO_ENABLE, 1);
  pluvio += pluviometro(); // aggiungiamo gli impulsi al pluviometro locale
  digitalWrite(IO_ENABLE, 0);

  tempoAttuale = millis();
  if (tempoAttuale - tempoTrascorso > sogliaTempo)
  {
    tempoTrascorso = tempoAttuale;

    //////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////// Raccolta letture sensori
    //////////////////////////////////////////////////////////

    wdt_reset();
#if DEBUG
    Serial.println("Inizio lettura sensori...");
#endif
    digitalWrite(IO_ENABLE, 1);
    wdt_reset();
    delay(300);

    // ad ogni ciclo tutte le variabili di lettura vengono azzerate
    float *sht3x_data = {};
    word sht31_1Temp = 0;
    word sht31_1Hum = 0;
    word sht31_2Temp = 0;
    word sht31_2Hum = 0;
    long peso1 = 0;
    long peso2 = 0;
    long pesoGrammi1 = 0;
    long pesoGrammi2 = 0;

    // A
#if DEBUG
    Serial.println("Porta A");
#endif
    digitalWrite(I2C_SWITCH, 1);
    sht3x_data = sht3x(SHT3X);
    sht31_1Temp = sht3x_data[0];
    sht31_1Hum = sht3x_data[1];
// B
#if DEBUG
    Serial.println("Porta B");
#endif
    digitalWrite(I2C_SWITCH, 0);
    sht3x_data = sht3x(SHT3X);
    sht31_2Temp = sht3x_data[0];
    sht31_2Hum = sht3x_data[1];
    /* I VALORI DEL PLUVIOMETRO VENGONO RACCOLTI IN CONTINUAZIONE FUORI DAL CICLO */
#if DEBUG
    Serial.print("Impulsi pluviometro raccolti: ");
    Serial.print(pluvio);
    Serial.println();
#endif
// C
#if DEBUG
    Serial.println("Porta C");
#endif
    peso1 = pesa(PORT_C_J_1_4, PORT_C_J_1_5, taraturaC);
    pesoGrammi1 = converti_peso(peso1);
    pesoGrammi1 = filtro_anti_zero(pesoGrammi1, pesoPrecedente1);
    pesoPrecedente1 = pesoGrammi1;
// D
#if DEBUG
    Serial.println("Porta D");
#endif
    peso2 = pesa(PORT_D_J_4_4, PORT_D_J_4_5, taraturaD);
    pesoGrammi2 = converti_peso(peso2);
    pesoGrammi2 = filtro_anti_zero(pesoGrammi2, pesoPrecedente2);
    pesoPrecedente2 = pesoGrammi2;
#if DEBUG
    Serial.println("Fine lettura sensori");
#endif
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
      msgSF[2] = 0xFF;
      msgSF[3] = 0xFE;
      msgSF[4] = 0xFF;
      msgSF[5] = 0xFE;
      msgSF[6] = sht31_2Temp;
      msgSF[7] = sht31_2Hum;
      msgSF[8] = 0xFF;
      msgSF[9] = 0xFE;
      msgSF[10] = vbat;
      msgSF[11] = 0xED;

      pinMode(SFOX_RST, OUTPUT);
      digitalWrite(SFOX_RST, 1);
      Radio.begin(SERIAL_SIGFOX);
      wdt_reset();
      delay(100);
      getID();
      delay(100);
      getPAC();
      wdt_reset();
      delay(5000);
      sendMessageSF(msgSF, 12);
      digitalWrite(SFOX_RST, 0);

#if DEBUG
      Serial.println("Messaggio SigFox inviato");
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
      msgL[10] = 0xFF;
      msgL[11] = 0xFE;
      //---------------------------------- A04         (SENSORE DALLAS 1)
      msgL[12] = 0xFF;
      msgL[13] = 0xFE;
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
      msgL[22] = 0xFF;
      msgL[23] = 0xFE;
      //---------------------------------- B10         (SENSORE DALLAS 2)
      msgL[24] = 0xFF;
      msgL[25] = 0xFE;
      //---------------------------------- B11         (Pluviometro)
      msgL[26] = !pluvio ? 0xFF : highByte(pluvio);
      msgL[27] = !pluvio ? 0xFE : lowByte(pluvio);
      //---------------------------------- B12         (Drenato)
      msgL[28] = !pluvio ? 0xFF : highByte(pluvio);
      msgL[29] = !pluvio ? 0xFE : lowByte(pluvio);
      //---------------------------------- C13         (Forchetta umidità 1)
      msgL[30] = 0xFF;
      msgL[31] = 0xFE;
      //---------------------------------- C14         (SENSORE DALLAS 3)
      msgL[32] = 0xFF;
      msgL[33] = 0xFE;
      //---------------------------------- C15         (Anemometro)
      msgL[34] = 0xFF;
      msgL[35] = 0xFE;
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
      msgL[44] = highByte((int)pesoGrammi1);
      msgL[45] = lowByte((int)pesoGrammi1);
      //---------------------------------- C21
      msgL[46] = 0xFF;
      msgL[47] = 0xFE;
      //---------------------------------- C22
      msgL[48] = 0xFF;
      msgL[49] = 0xFE;
      //---------------------------------- D23         (FORCHETTA umidità 2)
      msgL[50] = 0xFF;
      msgL[51] = 0xFE;
      //---------------------------------- D24         (SENSORE DALLAS 4)
      msgL[52] = 0xFF;
      msgL[53] = 0xFE;
      //---------------------------------- D25         (Segnavento)
      msgL[54] = 0xFF;
      msgL[55] = 0xFE;
      //---------------------------------- D26         (Sensore peso 2)
      msgL[56] = highByte(pesoGrammi2);
      msgL[57] = lowByte(pesoGrammi2);
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
      wdt_reset();
      delay(100);
      for (int j = 0; j < 70; j++)
        Radio.write(msgL[j]);

#if DEBUG
      Serial.println("Messaggio LORA inviato");
      for (int i = 0; i < int(sizeof(msgL) / sizeof(msgL[0])); i++)
      {
        Serial.print(msgL[i], HEX);
        Serial.print("|");
      }
      Serial.println();
#endif
    }
    buzzer(); // un solo bip per notificare l'invio del messaggio
    wdt_reset();
    delay(500);
    pluvio = 0;
#if DEBUG
    Serial.begin(BAUD);
    Serial.println("Riavvio ciclo");
#endif
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    wdt_disable();
    delay(200);
    wdt_enable(WDTO_8S);
    delay(200);
    contaCicli++;
  }

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
#if DEBUG
  Serial.print("Sigfox Device ID: ");
  Serial.println(id);
#endif

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
#if DEBUG
  Serial.println("Inside sendMessage");
#endif
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
#if DEBUG
  Serial.println("Sending...");
  Serial.println(sigfoxCommand);
#endif
  Radio.println(sigfoxCommand);
  while (!Radio.available())
  {
    wdt_reset();
#if DEBUG
    Serial.println("Waiting for response");
#endif
    wdt_reset();
    delay(1000);
  }
  while (Radio.available())
  {
    output = (char)Radio.read();
    status += output;
    delay(10);
  }
#if DEBUG
  Serial.println();
  Serial.print("Status \t");
  Serial.println(status);
#endif
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
  for (int i = 0; i <= 500; i++)
  {
    digitalWrite(BUZZER, 1);
    delayMicroseconds(200);
    digitalWrite(BUZZER, 0);
    delayMicroseconds(200);
  }
}