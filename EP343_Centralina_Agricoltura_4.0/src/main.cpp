#include <EEPROM.h>
#include <BluetoothSerial.h>
#include <Timer.h>
#include "VirtuinoCM.h"

#if!defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled!Please run `make menuconfig`
to and enable it
#endif

#define EEPROM_SIZE 5
#define EEPROM_START_LOCATION 1
#define EEPROM_END_LOCATION 2
#define EEPROM_STATE_LOCATION 3
#define EEPROM_DELAY_LOCATION 4
#define EEPROM_ORAMINUTISWITCH_LOCATION 10
//Pin mapping
#define RELAY_1 32
#define RELAY_2 33
#define LED_1 5
#define PULSANTE23 23
#define PULSANTE19 19
#define MILLIORA 3600000 // costante di un'ora in millisecondi
#define MILLIMINUTO 60000 // costante di un minuto in millisecondi
#define V_MEMORY_SIZE 12 // the size of V memory. You can change it to a number <=255)
#define IMPULSO 2000 // durata impulso in millisecondi

long V[V_MEMORY_SIZE]; // This array is synchronized with Virtuino V memory. You can change the type to int, long etc.
boolean debug = 0; // set this variable to false on the finale code to decrease the request time.
boolean pulsante23controllo = 0;
boolean controlloStato3 = 0;
boolean irrigazioneManualeControlloApp = 0;
boolean primaIrrigazione = 0; // stato della prima irrigazione, per settare il ritardo
boolean irrigazioneAuto;
boolean irrigazioneAutoInAtto = 0;
boolean statoLedInterno = 0;
unsigned int oreOminuti; // questa variabile prenderà i valori o di MILLIORA o di MILLIMINUTO (V[10])
byte stato = 1;
int sogliaIrrigazione = 0;
int valoreTempo;
unsigned short int impulsi = 0; // numero di impulsi da inviare quando l'irrigazione automatica è spenta
unsigned int secondiPassati; // variabile usata nel lampeggio del led interno

BluetoothSerial SerialBT;
VirtuinoCM virtuino;
Timer timerGlobale;
Timer timerManuale; // variabile timer irrigazione manuale, non è utilizzata per limiti dell'app non a pagamento
Timer timerRitardo;
Timer timerIntervallo;
Timer timerDurata;
//============================================================== onCommandReceived
//==============================================================
/* This function is called every time Virtuino app sends a request to server to change a Pin value
 * The 'variableType' can be a character like V, T, O  V=Virtual pin  T=Text Pin    O=PWM Pin 
 * The 'variableIndex' is the pin number index of Virtuino app
 * The 'valueAsText' is the value that has sent from the app   */
void onReceived(char variableType, uint8_t variableIndex, String valueAsText) {
  if (variableType == 'V') {
    float value = valueAsText.toFloat(); // convert the value to float. The valueAsText have to be numerical
    if (variableIndex < V_MEMORY_SIZE) V[variableIndex] = value; // copy the received value to arduino V memory array
  }
}

//==============================================================
/* This function is called every time Virtuino app requests to read a pin value*/
String onRequested(char variableType, uint8_t variableIndex) {
  if (variableType == 'V') {
    if (variableIndex < V_MEMORY_SIZE) return String(V[variableIndex]); // return the value of the arduino V memory array
  }
  return "";
}

//============================================================== virtuinoRun
void virtuinoRun() {
  while (SerialBT.available()) {
    char tempChar = SerialBT.read();
    if (tempChar == CM_START_CHAR) { // a new command is starting...
      virtuino.readBuffer = CM_START_CHAR; // copy the new command to the virtuino readBuffer
      virtuino.readBuffer += SerialBT.readStringUntil(CM_END_CHAR);
      virtuino.readBuffer += CM_END_CHAR;
      if (debug) Serial.println("\nCommand= " + virtuino.readBuffer);
      String * response = virtuino.getResponse(); // get the text that has to be sent to Virtuino as reply. The library will check the inptuBuffer and it will create the response text
      if (debug) Serial.println("Response : " + * response);
      SerialBT.print( * response);
      break;
    }
  }
}

//============================================================== vDelay
void vDelay(int delayInMillis) {
  long t = millis() + delayInMillis;
  while (millis() < t) virtuinoRun();
}
//============================================================== funzioni irrigazione
void accendi_irrigazione() {
  irrigazioneManualeControlloApp = 1;
  digitalWrite(RELAY_1, 1);
  digitalWrite(RELAY_2, 0);
  vDelay(IMPULSO);
  digitalWrite(RELAY_1, 0);
  digitalWrite(RELAY_2, 0);
}

void spegni_irrigazione() {
  irrigazioneManualeControlloApp = 0;
  digitalWrite(RELAY_1, 0);
  digitalWrite(RELAY_2, 1);
  vDelay(IMPULSO);
  digitalWrite(RELAY_1, 0);
  digitalWrite(RELAY_2, 0);
}

void timers_reset() {
  timerDurata.start();
  timerDurata.pause();
  timerIntervallo.start();
  timerIntervallo.pause();
}

//============================================================================

void setup() {

  pinMode(LED_1, OUTPUT);
  pinMode(RELAY_1, OUTPUT);
  pinMode(RELAY_2, OUTPUT);
  pinMode(PULSANTE19, INPUT);
  pinMode(PULSANTE23, INPUT);
  digitalWrite(LED_1, HIGH);
  digitalWrite(RELAY_1, LOW);
  digitalWrite(RELAY_2, LOW);

  EEPROM.begin(EEPROM_SIZE);

  Serial.begin(115200);
  SerialBT.begin("Melograni1"); //Bluetooth device name
  Serial.println("The device started, now you can pair it with bluetooth!");
  Serial.println("ESPertino Bluetooth control");

  virtuino.begin(onReceived, onRequested, 256); //Start Virtuino. Set the buffer to 256. With this buffer Virtuino can control about 28 pins (1 command = 9bytes) The T(text) commands with 20 characters need 20+6 bytes
  //virtuino.key="1234";                       //This is the Virtuino password. Only requests the start with this key are accepted from the library

  if (EEPROM.read(0) != 55) { // inizializza EEPROM
    EEPROM.write(0, 55);
    EEPROM.write(EEPROM_START_LOCATION, 12);
    EEPROM.write(EEPROM_END_LOCATION, 1);
    EEPROM.write(EEPROM_STATE_LOCATION, 1);
    EEPROM.write(EEPROM_DELAY_LOCATION, 12);
    EEPROM.write(EEPROM_ORAMINUTISWITCH_LOCATION, 1);
    EEPROM.commit();
  }

  // Assegnazione array variabili di Virtuino (comunicano con Virtuino)
  V[1] = EEPROM.read(EEPROM_START_LOCATION); // valore intervallo irrigazione
  V[2] = EEPROM.read(EEPROM_END_LOCATION); // valore durata irrigazione
  V[3] = EEPROM.read(EEPROM_STATE_LOCATION); // valore stato dispositivo (1 o 2)
  V[4] = 0; // valore indicatore irrigazione
  V[5] = 0; // valore timer globale
  V[9] = EEPROM.read(EEPROM_DELAY_LOCATION); // valore durata ritardo irrigazione
  V[10] = EEPROM.read(EEPROM_ORAMINUTISWITCH_LOCATION ); // valore scelta di misura del tempo in ore o minuti

  timerGlobale.start(); // inizializzazione timer
  timerGlobale.pause();
  timerIntervallo.start();
  timerIntervallo.pause();
} // Close Setup

void loop() {
  virtuinoRun(); // Necessary function to communicate with Virtuino. Client handler
  // enter your code below. Avoid to use delays on this loop. Instead of the default delay function use the vDelay that is located on the bottom of this code
  // You don't need to add code to read or write to the pins. Just enter the  pinMode of each Pin you want to use on void setup
  V[7] = timerGlobale.read();

  if (V[10]) oreOminuti = MILLIORA;
  else oreOminuti = MILLIMINUTO;

  // lampeggio led interno
  if (millis() - secondiPassati > 1000)
  {
    statoLedInterno = !statoLedInterno;
    digitalWrite(LED_1, statoLedInterno);
    secondiPassati = millis();
  }

  //Serial.println((V[1] * oreOminuti) - (timerIntervallo.read() % (V[1] * oreOminuti)));
  //========================================================= Stati e funzioni pulsanti

  /* se la durata di irrigazione è maggiore dell'intervallo, 
  la durata verà settata come l'intervallo stesso */ //if (V[2] > V[1]) V[2] = V[1]; 

  //==================================================================== controllo stati (1 o 2)
  if (((digitalRead(PULSANTE19) && digitalRead(PULSANTE23)) ||
     (digitalRead(PULSANTE19) && !digitalRead(PULSANTE23))) &&
     !controlloStato3) stato = 1; // stato modalità AUTO
  if (!digitalRead(PULSANTE19) && digitalRead(PULSANTE23)) stato = 2;

  switch (stato) 
  {
  case 1:
    ////////////////////////////////////////////////////////////////////// modalità automatica
    //Serial.println(stato);
    V[3] = 1; // salviamo il valore della modalità automatica
    irrigazioneAuto = 1;
    timerIntervallo.resume();
    sogliaIrrigazione = timerIntervallo.read();
    if (!digitalRead(PULSANTE23) && stato == 1); // SUCCEDE NIENTE!

    if (primaIrrigazione && V[9] != 00) valoreTempo = V[9]; // controllo prima irrigazione programmata
    else if (primaIrrigazione && V[9] == 0) sogliaIrrigazione = -1; 
    else valoreTempo = V[1];

    if (!irrigazioneAutoInAtto) // controllo ciclico spegnimento irrigazione, ogni 6 secondi manda un impulso di spegnimento per 3 volte
    {
      if (timerIntervallo.read() % 6000 == 0 && timerIntervallo.read() != 0 && impulsi < 3) 
      {
        spegni_irrigazione();
        impulsi++;
      }
    }

    if (sogliaIrrigazione >= valoreTempo * oreOminuti || sogliaIrrigazione == -1) // se la soglia di irrigazione raggiunge un valore vicino a quello dell'intervallo settato, parte l'irrigazione
    {
      controlloStato3 = true; // accendi irrigazione
	  if (primaIrrigazione) primaIrrigazione = 0;
      timerIntervallo.start();
      timerIntervallo.stop();
      stato = 3;
    }

    if (timerDurata.read() >= V[2] * oreOminuti && irrigazioneAutoInAtto) // fine irrigazione
    {
      spegni_irrigazione();
      Serial.println("SPEGNI IRRIGAZIONE (AUTO)");
      irrigazioneAutoInAtto = 0;
      timerDurata.start();
      timerDurata.stop();
      timerGlobale.pause();
      timerIntervallo.start();
    }

    V[5] = (V[2] * oreOminuti) - (timerDurata.read() % (V[2] * oreOminuti));
    V[6] = (valoreTempo * oreOminuti) - (timerIntervallo.read() % (valoreTempo * oreOminuti)); // visualizzatore tempo intervallo
    break;
  case 2:
    ////////////////////////////////////////////////////////////////////// modalità manuale
    //Serial.println(stato);
    V[3] = 2; // salviamo il valore della modalità manuale
    primaIrrigazione = true; // resetta il controllo ritardo irrigazione per la modalità automatica

    if (irrigazioneAuto)
    {
      spegni_irrigazione();
      timerGlobale.pause();
      irrigazioneAuto = 0;
    }

    timers_reset(); // resetta i timer di irrigazione
    impulsi = 0;

    if (!digitalRead(PULSANTE23) && stato == 2 && pulsante23controllo)
    {
      Serial.println("IRRIGAZIONE (MANUALE)");
      accendi_irrigazione();
      timerManuale.start();
      timerGlobale.resume();
      pulsante23controllo = !pulsante23controllo;
    } 

    if (digitalRead(PULSANTE23) && stato == 2 && !pulsante23controllo)
    {
      Serial.println("SPEGNI IRRIGAZIONE (MANUALE)");
      spegni_irrigazione();
      timerManuale.stop();
      timerGlobale.pause();
      pulsante23controllo = !pulsante23controllo;
    }
    break;
  case 3: 
    ////////////////////////////////////////////////////////////////////// irrigazione (ha uno stato a parte)
    if (!digitalRead(PULSANTE23)); // SUCCEDE NIENTE!
    Serial.println("ACCENDI IRRIGAZIONE (AUTO)");
    accendi_irrigazione();
    irrigazioneAutoInAtto = 1;
    impulsi = 0;
    timerGlobale.resume();
    timerDurata.start();
    controlloStato3 = false;
    break;
  }

  if (!irrigazioneManualeControlloApp) V[4] = 0;// spegne l'icona sull'app
  else V[4] = 1; // on in alternativa la accende
    
  //////////////////////////

  vDelay(10); // This is an example of the recommended delay function. Remove this if you don't need

  //==================================================================== aggiornamento valori in EEPROM
  if (EEPROM.read(EEPROM_START_LOCATION) != V[1]) EEPROM.write(EEPROM_START_LOCATION, V[1]);
  if (EEPROM.read(EEPROM_END_LOCATION) != V[2]) EEPROM.write(EEPROM_END_LOCATION, V[2]);
  if (EEPROM.read(EEPROM_STATE_LOCATION) != V[3]) EEPROM.write(EEPROM_STATE_LOCATION, V[3]);
  if (EEPROM.read(EEPROM_DELAY_LOCATION) != V[9]) EEPROM.write(EEPROM_DELAY_LOCATION, V[9]);
  if (EEPROM.read(EEPROM_ORAMINUTISWITCH_LOCATION ) != V[10]) EEPROM.write(EEPROM_ORAMINUTISWITCH_LOCATION , V[10]);
  EEPROM.commit();
} //Close Loop