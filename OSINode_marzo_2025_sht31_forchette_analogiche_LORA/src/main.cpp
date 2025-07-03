#include <Arduino.h>
#include "defines.h"           // header con tutte le definizioni di pin, indirizzi dispositivi...
#include "forchetta_umidita.h" // sensore umidità
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
SoftwareSerial Radio = SoftwareSerial(rxPin, txPin); // Definiamo la radio
bool forchettaPresente[2];
unsigned long int tempoAttuale;           // variabile per salvare i millis()
unsigned long int tempoTrascorso = 0;     // tempo che è passato dal precedente intervallo
unsigned long int tempoTrascorso1ora = 0; // tempo trascorso da 1 ora, per inviare il drenato
unsigned long int tempoMillisOreFreddo = 0;
unsigned long int tempoMillisOreFreddoTrascorso = millis();
int oraFreddo = 0;
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
  wdt_enable(WDTO_8S);

  for (int i = 0; i < 6; i++)
    dispositivoID[i] = EEPROM.read(i);

  ////////////////////////////////////////////////////////// Settaggio ID dispositivo
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
      Serial.println("Inserire nuovo ID ('Lxxxxx': ");
      int iID = 1;
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
  dispositivoID[0] = 'L';
  Serial.println();
  Serial.print("ID dispositivo: ");
  for (int i = 0; i < 6; i++) // aggiornamento ID dispositivo
    EEPROM.update(i, dispositivoID[i]);

  for (int i = 0; i < 6; i++)
    Serial.print(dispositivoID[i]);
  Serial.println();
  delay(1000);
  wdt_reset();

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  wdt_disable();
  wdt_enable(WDTO_8S);

  Wire.begin();

  // discrimina la presenza di forchette analogiche sulle porte C e D
  pinMode(PORT_C_J_1_3, INPUT_PULLUP);
  pinMode(PORT_D_J_4_3, INPUT_PULLUP);
  delay(10);
  forchettaPresente[0] = digitalRead(PORT_C_J_1_3) ? 0 : 1; // per qualche motivo le letture di presenza sensori sono invertite
  forchettaPresente[1] = digitalRead(PORT_D_J_4_3) ? 0 : 1;
#if DEBUG
  Serial.println(forchettaPresente[0] ? "Forchetta umidità presente su porta C" : "Forchetta umidità non presente su porta C");
  Serial.println(forchettaPresente[1] ? "Forchetta umidità presente su porta D" : "Forchetta umidità non presente su porta D");
#endif

  delay(1000);
  wdt_reset();
  buzzer(2);
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
  if (contaCicli != 0)
  {
    Serial.print("Tensione batteria: ");
    Serial.println(vbat);
  }
#endif
  digitalWrite(IO_ENABLE, 0);
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////// Raccolta letture sensori
  //////////////////////////////////////////////////////////

#if DEBUG
  Serial.println("Inizio lettura sensori...");
#endif
  digitalWrite(IO_ENABLE, 1);
  delay(300);
  for (int i = 0; i < 50; i++)
    delay(100);
  wdt_reset();

  // ad ogni ciclo tutte le variabili di lettura vengono azzerate
  float *sht3x_data = {};
  word sht31_1Temp = 0;
  word sht31_1Hum = 0;
  word sht31_2Temp = 0;
  word sht31_2Hum = 0;
  word humC = 0;
  word humD = 0;

  // A
#if DEBUG
  Serial.println("Porta A");
#endif
  digitalWrite(I2C_SWITCH, 1);
  sht3x_data = sht3x(SHT3X);
  sht31_1Temp = sht3x_data[0];
  sht31_1Hum = sht3x_data[1];
// B - l'sht su portale NECAP viene letto su porta B, quindi collegarlo qui
#if DEBUG
  Serial.println("Porta B");
#endif
  digitalWrite(I2C_SWITCH, 0);
  sht3x_data = sht3x(SHT3X);
  sht31_2Temp = sht3x_data[0];
  sht31_2Hum = sht3x_data[1];

  // calcolo ora di freddo sensori A e B
  if ((sht31_1Temp / 10) < SOGLIA_FREDDO || (sht31_2Temp / 10) < SOGLIA_FREDDO)
  {
    unsigned long int tempoMillisTrascorsoInRegistrazione = millis() - tempoMillisOreFreddoTrascorso;
    tempoMillisOreFreddo += (tempoMillisTrascorsoInRegistrazione + TEMPO_SLEEP_LORA);
#if DEBUG
    Serial.print("Millisecondi di freddo accumulati: ");
    Serial.print(tempoMillisOreFreddo);
    Serial.println();
#endif

    if (tempoMillisOreFreddo > UNA_ORA)
    {
#if DEBUG
      Serial.println("Nuova ora di freddo accumulata");
#endif
      oraFreddo = 1;
      tempoMillisOreFreddo = 0;
    }
    tempoMillisOreFreddoTrascorso = millis();
  }
// C
#if DEBUG
  Serial.println("Porta C");
#endif
  if (forchettaPresente[0])
  {
    digitalWrite(BOOST_SHTDWN, 1);
    wdt_reset();
    delay(10);
    humC = forchetta_umidita(PORT_C_J_1_5);
    digitalWrite(BOOST_SHTDWN, 0);
  }
  else
    humC = 0xFFFE;
// D
#if DEBUG
  Serial.println("Porta D");
#endif
  if (forchettaPresente[1])
  {
    digitalWrite(BOOST_SHTDWN, 1);
    delay(10);
    humD = forchetta_umidita(PORT_D_J_4_5);
    digitalWrite(BOOST_SHTDWN, 0);
  }
  else
    humD = 0xFFFE;

  Serial.println("Fine lettura sensori");
  delay(1000);
  wdt_reset();
  digitalWrite(IO_ENABLE, 0);
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////// Trasmissione
  //////////////////////////////////////////////////////////
  //-------------- MESSAGGIO LORA (MAX 70 BYTE)---------
#if DEBUG
  Serial.println("Preparazione ed invio messaggio LORA");
#endif
  byte msgL[70];
  // I primi 6 bytes contengono i caratteri dell'ID LORA
  for (int i = 0; i < 6; i++)
    msgL[i] = dispositivoID[i];
  //---------------------------------- A01         (Tensiometro)
  msgL[6] = 0xFF;
  msgL[7] = 0xFE;
  //---------------------------------- A02         (Luxmetro)
  msgL[8] = 0xFF;
  msgL[9] = 0xFE;
  //---------------------------------- A03         (OneWire temperatura)
  msgL[10] = 0xFF;
  msgL[11] = 0xFE;
  //---------------------------------- A04
  msgL[12] = 0xFF;
  msgL[13] = 0xFE;
  //---------------------------------- A05
  msgL[14] = highByte(oraFreddo);
  msgL[15] = lowByte(oraFreddo);
  //---------------------------------- A06
  msgL[16] = 0xFF;
  msgL[17] = 0xFE;
  //---------------------------------- B07         (Tensiometro 2)
  msgL[18] = 0xFF;
  msgL[19] = 0xFE;
  //---------------------------------- B08         (Luxmetro 2)
  msgL[20] = 0xFF;
  msgL[21] = 0xFE;
  //---------------------------------- B09         (OneWire temperatura suolo)
  msgL[22] = 0xFF;
  msgL[23] = 0xFE;
  //---------------------------------- B10         (Pluviometro)
  msgL[24] = 0xFF;
  msgL[25] = 0xFE;
  //---------------------------------- B11         (Drenato)
  msgL[26] = 0xFF;
  msgL[27] = 0xFE;
  //---------------------------------- B12
  msgL[28] = 0xFF;
  msgL[29] = 0xFE;
  //---------------------------------- C13
  msgL[30] = highByte(humC);
  msgL[31] = lowByte(humC);
  //---------------------------------- C14         (Anemometro)
  msgL[32] = 0xFF;
  msgL[33] = 0xFE;
  //---------------------------------- C15
  msgL[34] = 0xFF;
  msgL[35] = 0xFE;
  //---------------------------------- C16         (Forchetta analogica C)
  msgL[36] = highByte(humC);
  msgL[37] = lowByte(humC);
  //---------------------------------- C17         (SHT temperatura)
  msgL[38] = highByte(sht31_1Temp);
  msgL[39] = lowByte(sht31_1Temp);
  //---------------------------------- C18         (SHT umidità)
  msgL[40] = highByte(sht31_1Hum);
  msgL[41] = lowByte(sht31_1Hum);
  //---------------------------------- C19
  msgL[42] = 0xFF;
  msgL[43] = 0xFE;
  //---------------------------------- C20         (Sensore peso 1)
  msgL[44] = 0xFF;
  msgL[45] = 0xFE;
  //---------------------------------- C21
  msgL[46] = 0xFF;
  msgL[47] = 0xFE;
  //---------------------------------- C22
  msgL[48] = 0xFF;
  msgL[49] = 0xFE;
  //---------------------------------- D23         (SHT temperatura 2)
  msgL[50] = highByte(sht31_2Temp);
  msgL[51] = lowByte(sht31_2Temp);
  //---------------------------------- D24         (SHT umidità 2)
  msgL[52] = highByte(sht31_2Hum);
  msgL[53] = lowByte(sht31_2Hum);
  //---------------------------------- D25         (Forchetta umidità D)
  msgL[54] = highByte(humD);
  msgL[55] = lowByte(humD);
  //---------------------------------- D26         (Sensore peso 2)
  msgL[56] = 0xFF;
  msgL[57] = 0xFE;
  //---------------------------------- D27         (Direzione vento)
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

  Radio.end();

  Serial.println("Messaggio LORA inviato");
#if DEBUG
  for (int i = 0; i < int(sizeof(msgL) / sizeof(msgL[0])); i++)
  {
    Serial.print(msgL[i], HEX);
    Serial.print("|");
  }
  Serial.println();
#endif
  delay(3000);
  wdt_reset();
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
  pinMode(PORT_C_J_1_3, INPUT_PULLUP);
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
  pinMode(PORT_D_J_4_3, INPUT_PULLUP);
  digitalWrite(SDA_PIN, 1); // pinMode(A4, INPUT);//PER NON FAR CONSUMARE I BMP IN SLEEP
  digitalWrite(SCL_PIN, 1); // pinMode(A5, INPUT);//PER NON FAR CONSUMARE I BMP IN SLEEP

#if DEBUG
  Serial.println("Attesa...");
#endif
  buzzer(0);
  delay(1000);
  wdt_reset();
  for (int i = 0; i < TEMPO_LORA; i++)
    LowPower.powerDown(SLEEP_4S, ADC_OFF, BOD_OFF);

  // 15 minuti * 60 secondi = 900 secondi / 4 secondi = 225 iterazioni
  // 5 minuti * 60 secondi = 300 secondi / 4 secondi = 75 iterazioni

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  delay(1000);
  wdt_reset();
  contaCicli++;
  delay(250);
  oraFreddo = 0;
#if DEBUG
  Serial.println("Riavvio ciclo");
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
void buzzer(int times)
{
  for (int i = 0; i <= times; i++)
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