#include <Arduino.h>
#include <ArduinoLowPower.h>
#include <OneWire.h>
#include <FlashStorage_SAMD.h>
#include "defines.h"
#include "wiring_private.h"
#include "sht3x.h"
#include "batteria.h"
#include "rs485.h"
#include "misura_peso.h"
#include "sensore1wire.h"

void SERCOM1_Handler();
void pluvio_ISR();
void wake_up();
void go_sleep(uint8_t mins);
void buzzer(int times);
String command(String command);
void sendMessage(uint8_t msg[], int size);

OneWire sens1wireC(PIN_CENTRALE_C);
OneWire sens1wireD(PIN_CENTRALE_D);
Uart radio(&sercom1, PIN_RADIO_RX, PIN_RADIO_TX, SERCOM_RX_PAD_0, UART_TX_PAD_2); // definizione radio, sercom 1
TwoWire iQuadroCi(&sercom3, PIN_SDA, PIN_SCL);                                    // definizione I2C

volatile unsigned long int pluvioCount = 0;
volatile unsigned long int tempoUltimoImpulso = 0;
bool taratura = 0; // taratura sensori peso al primo ciclo
uint8_t msgL[70];
uint8_t msgS[12];
bool sigfoxLora = 0; // 0 = sigfox, 1 = lora
unsigned long int contaCicli = 0;
bool presenzaOneWireC = 0;
bool presenzaOneWireD = 0;
byte indirizzo1wireC[8];
byte tipo1wireC;
byte indirizzo1wireD[8];
byte tipo1wireD;
String dispositivoID;

void setup()
{
  // al primo avvio resetta la memoria flash (emulata a EEPROM stile arduino)
  if (EEPROM.read(64) != 64)
  {
    EEPROM.write(64, 64);
    EEPROM.write(0, 'L');
    for (int i = 1; i < 6; i++)
      EEPROM.write(i, '0');
    EEPROM.commit();
  }

  delay(2000);
  Serial.begin(BAUD);
  radio.begin(SIGFOX_BAUD);
  iQuadroCi.begin();
  Serial.println("Rilevamento sensori OneWire su porte C e D...");
  for (int i = 0; i < 10; i++)
  {
    if (init_1wire(sens1wireC, indirizzo1wireC, tipo1wireC))
    {
      presenzaOneWireC = 1;
      break;
    }
  }
  for (int i = 0; i < 10; i++)
  {
    if (init_1wire(sens1wireD, indirizzo1wireD, tipo1wireD))
    {
      presenzaOneWireD = 1;
      break;
    }
  }

  battery_init();

  delay(200);
  pinPeripheral(PIN_RADIO_TX, PIO_SERCOM);
  pinPeripheral(PIN_RADIO_RX, PIO_SERCOM);
  delay(200);
  pinMode(PIN_SIGFOX_RESET, OUTPUT);
  delay(200);
  digitalWrite(PIN_SIGFOX_RESET, 1);
  delay(5000); // DEBUG

  Serial.println("Rilevamento modulo radio (SigFox o LoRa)...");
  String rispostaModulo = command("AT$I=11\r");
  Serial.println(rispostaModulo);
  delay(1000);
  if (rispostaModulo == "")
  {
    Serial.println("Modulo LoRa installato");
    radio.end();
    sigfoxLora = 1;
    delay(3000);
    radio.begin(RADIO_BAUD);

    for (int i = 0; i < 6; i++)
      EEPROM.get(i, dispositivoID[i]);

    ////////////////////////////////////////////////////////// Settaggio ID dispositivo
    Serial.print("ID dispositivo in memoria: ");
    Serial.print((String)dispositivoID);
    Serial.println();
    Serial.println("Cambiare ID e protocollo dispositivo? (premere entro 5 secondi 's' o premere qualsiasi altro tasto per procedere)");
    unsigned int tempoEditDispositivo = millis();
    while (!Serial.available() && millis() - tempoEditDispositivo < 5000)
      ; // attende 5 secondi per la pressione del tasto 's'
    if (Serial.available() > 0)
    {
      if (Serial.read() == 's')
      {
        Serial.println("Inserire nuovo ID ('Lxxxxx'): ");
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
    EEPROM.commit();
    for (int i = 0; i < 6; i++)
      Serial.print(dispositivoID[i]);
    Serial.println();
    delay(1000);
  }
  else
    Serial.println("Modulo SigFox installato");

  pinPeripheral(PIN_PLUVIO_A, PIO_EXTINT);
  pinPeripheral(PIN_PLUVIO_B, PIO_EXTINT);
  LowPower.attachInterruptWakeup(PIN_PLUVIO_A, pluvio_ISR, FALLING);
  LowPower.attachInterruptWakeup(PIN_PLUVIO_B, pluvio_ISR, FALLING);
  attachInterrupt(PIN_PLUVIO_A, pluvio_ISR, FALLING);
  attachInterrupt(PIN_PLUVIO_B, pluvio_ISR, FALLING);

  pinPeripheral(PIN_SDA, PIO_SERCOM);
  pinPeripheral(PIN_SCL, PIO_SERCOM);
  pinPeripheral(FORKETT_RX, PIO_SERCOM);
  pinPeripheral(FORKETT_TX, PIO_SERCOM);
  pinPeripheral(FORKETT_RX2, PIO_SERCOM_ALT);
  pinPeripheral(FORKETT_TX2, PIO_SERCOM_ALT);

  pinMode(PIN_CENTRALE_C, INPUT_PULLUP);
  pinMode(PIN_CENTRALE_D, INPUT_PULLUP);
  pinMode(PIN_PLUVIO_A, INPUT);
  pinMode(PIN_PLUVIO_B, INPUT);
  pinMode(PIN_FORCHETTA_C, INPUT_PULLUP);
  pinMode(PIN_FORCHETTA_D, INPUT_PULLUP);

  pinMode(BOOST_EN, OUTPUT);
  pinMode(BOOST_SHTDWN, OUTPUT);
  pinMode(IO_ENABLE, OUTPUT);
  pinMode(I2C_SELECT, OUTPUT);
  pinMode(PIN_TX_ENABLE, OUTPUT);
  pinMode(PIN_SDA_C, OUTPUT);
  pinMode(PIN_SDA_D, OUTPUT);
  digitalWrite(BOOST_EN, 0);
  digitalWrite(BOOST_SHTDWN, 0);
  digitalWrite(PIN_SIGFOX_RESET, 0);
}

void loop()
{
  if (contaCicli != 0)
    wake_up();
  buzzer(1);

  uint16_t batteria = 0;
  uint16_t forchettaAnalogC = 0;
  uint16_t forchettaAnalogD = 0;
  unsigned long taraturaC; // taratura peso sulla porta C
  unsigned long taraturaD; // taratura peso sulla porta D
  unsigned long pesoPrecedente1 = 0;
  unsigned long pesoPrecedente2 = 0;
  long peso1 = 0;
  long peso2 = 0;
  long pesoGrammi1 = 0;
  long pesoGrammi2 = 0;
  uint16_t rs485TempE = 0;
  uint16_t rs485HumE = 0;
  /* uint16_t rs485TempF = 0;
  uint16_t rs485HumF = 0; */
  uint8_t rs485risultati[11] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t shtTempA = 0;
  uint8_t shtHumA = 0;
  uint16_t shtTempB = 0;
  uint16_t shtHumB = 0;
  uint8_t *valori = nullptr;
  float temperatura1wireC = 0;
  float temperatura1wireD = 0;

  Serial.print("Ciclo: ");
  Serial.print(contaCicli);
  Serial.println();

  batteria = battery_read();
  Serial.print("Lettura batteria: ");
  Serial.print(batteria);
  Serial.println();

  Serial.print("Dati pluviometro: ");
  Serial.print(pluvioCount);
  Serial.println();

  Serial.println("Inizio lettura sensori su porte A e B");
  digitalWrite(IO_ENABLE, 1);
  delay(100);

  digitalWrite(I2C_SELECT, 1); // scansione su porta A
  delay(100);
  if (i2c_scan_sht3x(iQuadroCi))
  {
    valori = sht3x(SHT3X_ADDRESS);
    shtTempA = valori[0];
    shtHumA = valori[1];
  }

  digitalWrite(I2C_SELECT, 0); // scansione su porta B
  delay(100);
  if (i2c_scan_sht3x(iQuadroCi))
  {
    valori = sht3x(SHT3X_ADDRESS);
    shtTempB = valori[0];
    shtHumB = valori[1];
  }

  digitalWrite(BOOST_EN, 1);
  delay(100);

  if (!presenzaOneWireC)
  {
    Serial.println("Rilevamento forchette analogiche su porte C e D");
    if (!digitalRead(PIN_CENTRALE_C))
    {
      digitalWrite(BOOST_SHTDWN, 1);
      Serial.println("Forchetta umidità presente su porta C");
      Serial.print("Valore: ");
      forchettaAnalogC = analogRead(PIN_FORCHETTA_C);
      Serial.print(forchettaAnalogC);
      Serial.println();
    }
    else
    {
      digitalWrite(BOOST_SHTDWN, 0);
      delay(10);
      Serial.println("Forchetta umidità non presente su porta C");
      Serial.println("Misurazione peso su porta C");
      if (!taratura)
      {
        Serial.println("Taratura sensori peso su porta C:");
        taraturaC = pesa(PIN_SDA_C, PIN_FORCHETTA_C, 0);
        delay(100);
      }
      else
      {
        peso1 = pesa(PIN_SDA_C, PIN_FORCHETTA_C, taraturaC);
        pesoGrammi1 = converti_peso(peso1);
        pesoGrammi1 = filtro_anti_zero(pesoGrammi1, pesoPrecedente1);
        pesoPrecedente1 = pesoGrammi1;
      }
    }
  }
  else
  {
    temperatura1wireC = read_1wire(sens1wireC, indirizzo1wireC, tipo1wireC);
  }

  if (!presenzaOneWireD)
  {
    if (!digitalRead(PIN_CENTRALE_D))
    {
      digitalWrite(BOOST_SHTDWN, 1);

      Serial.println("Lettura forchette RS485");
      // forkett.begin(FORKETT_BAUD);
      forkett2.begin(FORKETT_BAUD);
      delay(300);
      // rs485TempE = rs485(forkett, 0);
      // rs485HumE = rs485(forkett, 1);
      /* rs485TempF = rs485(forkett2, 1);
      rs485HumF = rs485(forkett2, 0); */
      rs485(forkett2, rs485risultati);

      if (!((rs485risultati[5] << 8) | rs485risultati[6])) // if (!rs485TempF)
      {
        Serial.println("Forchetta umidità presente su porta D");
        Serial.print("Valore: ");
        forchettaAnalogD = analogRead(PIN_FORCHETTA_D);
        Serial.print(forchettaAnalogD);
        Serial.println();
      }
    }
    else
    {
      digitalWrite(BOOST_SHTDWN, 0);
      delay(10);
      Serial.println("Forchetta umidità non presente su porta D");
      Serial.println("Misurazione peso su porta D");
      if (!taratura)
      {
        Serial.println("Taratura sensori peso su porta D:");
        taraturaD = pesa(PIN_SDA_D, PIN_FORCHETTA_D, 0);
        delay(100);
      }
      else
      {
        peso2 = pesa(PIN_SDA_D, PIN_FORCHETTA_D, taraturaD);
        pesoGrammi2 = converti_peso(peso2);
        pesoGrammi2 = filtro_anti_zero(pesoGrammi2, pesoPrecedente2);
        pesoPrecedente2 = pesoGrammi2;
      }
    }
  }
  else
  {
    temperatura1wireD = read_1wire(sens1wireD, indirizzo1wireD, tipo1wireD);
  }

  if (sigfoxLora)
  {
    // I primi 6 bytes contengono i caratteri dell'ID LORA
    msgL[0] = dispositivoID[0];
    msgL[1] = dispositivoID[1];
    msgL[2] = dispositivoID[2];
    msgL[3] = dispositivoID[3];
    msgL[4] = dispositivoID[4];
    msgL[5] = dispositivoID[5];
    //---------------------------------- A01         (SHT31 Temperatura 1)
    msgL[6] = highByte(shtTempA);
    msgL[7] = lowByte(shtTempA);
    //---------------------------------- A02         (SHT31 Umidità 1)
    msgL[8] = highByte(shtHumA);
    msgL[9] = lowByte(shtHumA);
    //---------------------------------- A03         (Luxmetro 1)
    msgL[10] = highByte(65534);
    msgL[11] = lowByte(65534);
    //---------------------------------- A04         (SENSORE DALLAS 1)
    msgL[12] = highByte(65534);
    msgL[13] = lowByte(65534);
    //---------------------------------- A05
    msgL[14] = highByte(65534);
    msgL[15] = lowByte(65534);
    //---------------------------------- A06
    msgL[16] = highByte(65534);
    msgL[17] = lowByte(65534);
    //---------------------------------- B07         (SHT31 Temperatura 2)
    msgL[18] = highByte(shtTempB);
    msgL[19] = lowByte(shtHumB);
    //---------------------------------- B08         (SHT31 Umidità 2)
    msgL[20] = highByte(65534);
    msgL[21] = lowByte(65534);
    //---------------------------------- B09         (Luxmetro 2)
    msgL[22] = highByte(65534);
    msgL[23] = lowByte(65534);
    //---------------------------------- B10         (SENSORE DALLAS 2)
    msgL[24] = highByte(65534);
    msgL[25] = lowByte(65534);
    //---------------------------------- B11         (Pluviometro)
    msgL[26] = highByte(pluvioCount);
    msgL[27] = lowByte(pluvioCount);
    //---------------------------------- B12         (Drenato)
    msgL[28] = highByte(pluvioCount);
    msgL[29] = lowByte(pluvioCount);
    //---------------------------------- C13         (Forchetta umidità 1)
    msgL[30] = highByte(forchettaAnalogC);
    msgL[31] = lowByte(forchettaAnalogC);
    //---------------------------------- C14         (SENSORE DALLAS 3)
    msgL[32] = highByte((int)temperatura1wireC);
    msgL[33] = lowByte((int)temperatura1wireC);
    //---------------------------------- C15         (Anemometro)
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
    //---------------------------------- C20         (Sensore peso 1)
    msgL[44] = highByte(pesoGrammi1);
    msgL[45] = lowByte(pesoGrammi1);
    //---------------------------------- C21
    msgL[46] = highByte(65534);
    msgL[47] = lowByte(65534);
    //---------------------------------- C22
    msgL[48] = highByte(65534);
    msgL[49] = lowByte(65534);
    //---------------------------------- D23         (FORCHETTA umidità 2)
    msgL[50] = highByte(forchettaAnalogD);
    msgL[51] = lowByte(forchettaAnalogD);
    //---------------------------------- D24         (SENSORE DALLAS 4)
    msgL[52] = highByte((int)temperatura1wireD);
    msgL[53] = lowByte((int)temperatura1wireD);
    //---------------------------------- D25         (Segnavento)
    msgL[54] = highByte(65534);
    msgL[55] = lowByte(65534);
    //---------------------------------- D26         (Sensore peso 2)
    msgL[56] = highByte(pesoGrammi2);
    msgL[57] = lowByte(pesoGrammi2);
    //---------------------------------- D27
    msgL[58] = highByte(65534);
    msgL[59] = lowByte(65534);
    //---------------------------------- D28
    msgL[60] = highByte(65534);
    msgL[61] = lowByte(65534);
    //---------------------------------- D29           (CICLI del firmware dall'accensione)
    msgL[62] = highByte(contaCicli);
    msgL[63] = lowByte(contaCicli);
    //---------------------------------- BAT30         (BATTERIA)
    msgL[64] = 0x00;
    msgL[65] = (batteria - 1500) / 8;
    //----------------------------------- RC
    msgL[66] = 50; // sostituire con valore randomico
    //------------------------------ END
    msgL[70 - 3] = 0;
    msgL[70 - 2] = 255;
    msgL[70 - 1] = 255;
    delay(500);

    Serial.println("Messaggio:");
    for (int i = 0; i < 70; i++)
    {
      Serial.print(msgL[i], HEX);
      Serial.print("|");
    }
    Serial.println();

    Serial.println("Invio messaggio LoRa...");
    for (int i = 0; i < 70; i++)
    {
      radio.write(msgL[i]);
    }
    delay(1000);
  }
  else
  {
    for (int i = 0; i < 12; i++)
      msgS[i] = 0;

    msgS[0] = 0xA1;
    msgS[1] = pluvioCount;
    msgS[2] = /* rs485HumE != 0 ? */ highByte(rs485HumE) /* : highByte(forchettaAnalogC) */;
    msgS[3] = /* rs485HumE != 0 ? */ lowByte(rs485HumE) /* : lowByte(forchettaAnalogC) */;
    msgS[4] = /* rs485HumF != 0 ? */ rs485risultati[3] /* : highByte(forchettaAnalogD) */;
    msgS[5] = /* rs485HumF != 0 ? */ rs485risultati[4] /* : lowByte(forchettaAnalogD) */;
    msgS[6] = shtTempA ? shtTempA : shtTempB;
    msgS[7] = shtHumA ? shtHumA : shtHumB;
    msgS[8] = lowByte(rs485TempE);
    msgS[9] = rs485risultati[6];
    msgS[10] = (batteria - 1500) / 8; // (x - 1500) / 8 serve a comprimere il dato in un byte
    msgS[11] = 0xED;

    Serial.println("Messaggio SigFox: ");
    for (int i = 0; i < 12; i++)
    {
      Serial.print(msgS[i], HEX);
      Serial.print("|");
    }
    Serial.println();
    delay(500);

    Serial.println("ID dispositivo SigFox:");
    Serial.println(command("AT$I=10\r")); // otteniamo l'id
    delay(100);
    Serial.println("PAC dispositivo SigFox:");
    Serial.println(command("AT$I=11\r")); // otteniamo il pac number
    delay(100);
    sendMessage(msgS, 12);
  }

  taratura = 1;
  contaCicli++;
  pluvioCount = 0;
  buzzer(0);
  go_sleep(1);
}

void pluvio_ISR() // callback interrupt pluviometro
{
  unsigned long int ora = millis();
  if (ora - tempoUltimoImpulso > 100)
  {
    // buzzer(0); // DEBUG
    pluvioCount++;
    // buzzer(3); // DEBUG
    tempoUltimoImpulso = ora;
  }
}

void go_sleep(uint8_t mins)
{
  Serial.println("Entrata risparmio energetico");

  // spegne le SerCom a basso livello
  PM->APBCMASK.bit.SERCOM1_ = 0;
  PM->APBCMASK.bit.SERCOM3_ = 0;
  PM->APBCMASK.bit.SERCOM5_ = 0;
  Serial.end();

  // togliamo la corrente a tutti i pin in output, per risparmiare energia in modalità sleep
  digitalWrite(BOOST_EN, 0);
  digitalWrite(BOOST_SHTDWN, 0);
  // digitalWrite(IO_ENABLE, 0);
  digitalWrite(I2C_SELECT, 0);
  digitalWrite(PIN_TX_ENABLE, 0);
  digitalWrite(PIN_SDA_C, 0);
  digitalWrite(PIN_SDA_D, 0);
  /*   digitalWrite(PIN_RADIO_TX, 0);
    digitalWrite(PIN_RADIO_RX, 0); */
  digitalWrite(PIN_SDA, 0);
  digitalWrite(PIN_SCL, 0);
  digitalWrite(FORKETT_RX, 0);
  digitalWrite(FORKETT_TX, 0);
  digitalWrite(FORKETT_RX2, 0);
  digitalWrite(FORKETT_TX2, 0);
  digitalWrite(PIN_SIGFOX_RESET, 0);
  // mettiamo in input i pin interessati
  /*   pinMode(BOOST_EN, INPUT_PULLUP);
    pinMode(BOOST_SHTDWN, INPUT_PULLUP);
    pinMode(I2C_SELECT, INPUT_PULLUP);
    pinMode(PIN_TX_ENABLE, INPUT_PULLUP);
    pinMode(PIN_SDA_C, INPUT_PULLUP);
    pinMode(PIN_SDA_D, INPUT_PULLUP); */

  unsigned int tempoSleep = !sigfoxLora ? /* 8 */ 112 : 60; // se sigfox, dorme per 15 minuti, se lora, dorme per 8
  for (unsigned int i = 0; i < tempoSleep; i++)
    LowPower.deepSleep(8000);
}

void wake_up()
{
  Serial.begin(BAUD);
  Serial.println("Risveglio, riattivazione periferiche...");

  // riattiva le SerCom a basso livello (radio, forchette)
  PM->APBCMASK.bit.SERCOM1_ = 1;
  PM->APBCMASK.bit.SERCOM3_ = 1;
  PM->APBCMASK.bit.SERCOM5_ = 1;
  delay(1);

  attachInterrupt(PIN_PLUVIO_A, pluvio_ISR, FALLING);
  attachInterrupt(PIN_PLUVIO_B, pluvio_ISR, FALLING);

  pinMode(BOOST_EN, OUTPUT);
  pinMode(BOOST_SHTDWN, OUTPUT);
  // pinMode(IO_ENABLE, OUTPUT);
  pinMode(I2C_SELECT, OUTPUT);
  pinMode(PIN_TX_ENABLE, OUTPUT);
  pinMode(PIN_SDA_C, OUTPUT);
  pinMode(PIN_SDA_D, OUTPUT);
  pinMode(PIN_SIGFOX_RESET, OUTPUT);
  digitalWrite(BOOST_EN, 0);
  digitalWrite(BOOST_SHTDWN, 0);
  digitalWrite(PIN_SIGFOX_RESET, 0);

  battery_init(); // inizializziamo tutti i dispositivi da inizializzare...
  iQuadroCi.begin();
}

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

void sendMessage(uint8_t msg[], int size)
{
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
  radio.println(sigfoxCommand);

  uint8_t contateur = 0;
  while (!radio.available())
  {
    Serial.println("Waiting for response");
    contateur++;
    delay(1000);
    if (contateur > 30)
    {
      Serial.println("Device SigFox non pronto");
      return;
    }
  }

  while (radio.available())
  {
    output = (char)radio.read();
    status += output;
    delay(10);
  }
  digitalWrite(PIN_SIGFOX_RESET, 0);

  Serial.println();
  Serial.print("Status \t");
  Serial.println(status);
}

String command(String command)
{
  String resp = "";
  pinMode(PIN_SIGFOX_RESET, OUTPUT);
  delay(50);
  digitalWrite(PIN_SIGFOX_RESET, 1);
  delay(200);
  // Serial.println(command.length());
  /* for (unsigned int i = 0; i < command.length(); i++)
  {
      device.write(command.charAt(i));
      delayMicroseconds(200);
  } */
  radio.print(command);
  delay(1000);
  while (radio.available())
    resp += (char)radio.read();
  digitalWrite(PIN_SIGFOX_RESET, 0);
  return resp;
}

extern "C" void SERCOM1_Handler()
{
  // Chiama il metodo IrqHandler() della classe Uart, non tua funzione personalizzata
  radio.IrqHandler();
}