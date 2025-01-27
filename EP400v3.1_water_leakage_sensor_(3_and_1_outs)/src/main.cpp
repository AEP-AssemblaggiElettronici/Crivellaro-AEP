#include <Arduino.h>
#include <ModbusRTUSlave.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <Timer.h>
#include <avr/wdt.h>
#include <Wire.h>

#define DRIVER_ENABLE_PIN 2               // Pin di controllo su EP400
#define RXPIN 10                          // ModBus, pin di ricezione
#define TXPIN 11                          // ModBus, pin di trasmissione
#define LED_RED 13                        // Led rosso
#define LED_GREEN 17                      // Led verde
#define MODBUS_BUTTON 8                   // Pulsante funzione ModBus
#define SENSITIVITY_BUTTON 9              // Pulsante funzione sensibilità
#define MODBUS_SLAVE_ID_EEPROM_LOCATION 1 // ModBus Slave ID EEPROM locazione di memoria
#define SENSITIVITY_EEPROM_LOCATION 2     // EEPROM parametro sensibilità locazione di memoria
#define ANALOG_IN1 A0                     // Pin input analogici dei sensori
#define ANALOG_IN2 A1
#define ANALOG_IN3 A2
#define ALARM_PIN 7
#define ALARM_LED_1 3
#define ALARM_LED_2 4
#define ALARM_LED_3 5
#define NO_INPUT_THRESHOLD 1010
#define PCA9536D_ADDRESS 0x41
const uint8_t inputs[3] = {2, 3, 4}; // definiamo i pin di input

SoftwareSerial mySerial(RXPIN, TXPIN);              // comunicazione softwareSerial
ModbusRTUSlave modbus(mySerial, DRIVER_ENABLE_PIN); // definiamo ModBus RTU slave
Timer timerModbus;                                  // timer per l'edit mode del ModBus slave ID (cioè, per quanto tempo tieni premuto il pulsante?)
Timer timerSensitivity;                             // come sopra ma per quanto riguarda l'editing della sensibilità
Timer timerReboot;                                  // timer che resetta il dispositivo dopo 10 secondi in modalità di modifica slave ID
Timer sensorsTimer;                                 // come sopra ma per quanto riguarda l'editing della sensibilità

/* defining ModBus inputs, registers.. */
bool discreteInputs[6];
uint16_t commands[3];

uint8_t slaveID;                          // slave ID ModBus, default 1
int sensitivityValue;                     // valore sensibilità
uint8_t sensitivityValueIndex = 0;        // indice per l'array del valore della sensibilità
bool modbusEditMode = false;              // variabile di controllo per l'editing dello slave ID
bool modbusButtonLastStatus;              // variabile di controllo per il rilascio del pulsante ModBus
bool sensitivityEditMode = false;         // variabile di controllo per l'editing della sensibilità
bool sensitivityButtonLastStatus;         // variabile di controllo per il rilascio del pulsante sensibilità
int Analog_Value[3] = {1024, 1024, 1024}; // valori analogici dei sensori
int sensorsInterval = 4000;               // intervallo in millisecondi di lettura dei sensori
int sensitivityConsts[3] = {350, 500, 650};
bool alarm = false;
bool alarmSupport = false;
uint8_t units_ = 0;    // ModBus slave ID: cifra delle unità
uint8_t tens_ = 0;     // ModBus slave ID: cifra delle decine (ma anche centinaia)
bool editUnits = true; // variabile di controllo per passare dalla modalità di modifica unità o decine dello slave ID
uint8_t sensorsPresenceByte;
bool sensorsPresence[3];

void (*reboot)() = 0; // funzione di reset, all'indirizzo 0 manda in reset il dispositivo
void firstBoot();
void blinky();
void shortBlinky();
void analog_average(int, int);
void blinky_show();
void show_sensitivity();
bool check_input_connection(int);
void check_inputs_state();
void check_inputs_presence();
void leds_startup();

void setup()
{
    Serial.begin(9600); // DEBUG
    firstBoot();

    slaveID = EEPROM.read(MODBUS_SLAVE_ID_EEPROM_LOCATION);
    sensitivityValueIndex = EEPROM.read(SENSITIVITY_EEPROM_LOCATION);
    sensitivityValue = sensitivityConsts[sensitivityValueIndex];

    pinMode(inputs[0], INPUT_PULLUP);
    pinMode(inputs[1], INPUT_PULLUP);
    pinMode(inputs[2], INPUT_PULLUP);

    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(MODBUS_BUTTON, INPUT);
    pinMode(SENSITIVITY_BUTTON, INPUT);
    pinMode(ALARM_PIN, OUTPUT);
    pinMode(ALARM_LED_1, OUTPUT);
    pinMode(ALARM_LED_2, OUTPUT);
    pinMode(ALARM_LED_3, OUTPUT);

    digitalWrite(LED_GREEN, 1);
    digitalWrite(LED_RED, 1);

    check_inputs_presence();
    leds_startup();

    modbus.configureDiscreteInputs(discreteInputs, 6); // array booleano che indica lo stato degli input
    modbus.configureHoldingRegisters(commands, 3);     // i primi due numeri sono usati per indicare il comando, il terzo per un ipotetico valore (in HEX)
    modbus.begin(slaveID, 9600);

    timerModbus.start(); // avviamo i timer
    timerSensitivity.start();
    sensorsTimer.start();
    delay(100);
}

void loop()
{
    // ogni 4 secondi i sensori raccolgono i dati
    if (sensorsTimer.read() % sensorsInterval < 500)
    {
        for (int i = 0; i < 6; i++) // reset preventivo, per aggiornare lo stato sensori
            discreteInputs[i] = 0;

        check_inputs_presence();
        check_inputs_state();
    }

    // ipotetico blink dei led di allarme (se i sensori sono collegati e la soglia è superata)
    if (millis() % 500 < 250)
    {
        if ((discreteInputs[0] || discreteInputs[3]) && !sensorsPresence[0])
            digitalWrite(ALARM_LED_1, 0);
        else
            digitalWrite(ALARM_LED_1, 1);
        if ((discreteInputs[1] || discreteInputs[4]) && !sensorsPresence[1])
            digitalWrite(ALARM_LED_2, 0);
        else
            digitalWrite(ALARM_LED_2, 1);
        if ((discreteInputs[2] || discreteInputs[5]) && !sensorsPresence[2])
            digitalWrite(ALARM_LED_3, 0);
        else
            digitalWrite(ALARM_LED_3, 1);
        if (alarm)
            digitalWrite(LED_RED, 0);
    }
    else
    {
        digitalWrite(ALARM_LED_1, 1);
        digitalWrite(ALARM_LED_2, 1);
        digitalWrite(ALARM_LED_3, 1);
        digitalWrite(LED_RED, 1);
    }

    // valutazione dei sensori
    if (!discreteInputs[0] &&
        !discreteInputs[1] &&
        !discreteInputs[2] &&
        !discreteInputs[3] &&
        !discreteInputs[4] &&
        !discreteInputs[5])
    { // tutto OK
        alarm = false;
        digitalWrite(ALARM_PIN, 1);
        digitalWrite(LED_RED, 1);
        if (millis() % 4000 < 300 &&
            (!modbusEditMode && !sensitivityEditMode))
        {
            digitalWrite(LED_GREEN, 0);
            delay(10);
            digitalWrite(LED_GREEN, 1);
            delay(100);
            digitalWrite(LED_GREEN, 0);
        }
        else
            digitalWrite(LED_GREEN, 1);
    }
    else // ALLARME!
    {
        alarm = true;
        digitalWrite(ALARM_PIN, 0);
        digitalWrite(LED_GREEN, 1);
    }

    modbus.poll();

    //***************************************************COMANDI MODBUS:  ***************************************************************************/

    if (commands[0] == 0x00C0 && commands[1] == 0x0050)
    { // C050, per modificare lo slave ID
        if (commands[2] < 0xE6)
        {
            EEPROM.update(1, commands[2]);
        }
        else
            EEPROM.update(1, 1);
        blinky();
        delay(1000);
        reboot();
    }

    if (commands[0] == 0x0001 && commands[1] == 0x0001 && commands[2] == 0x0001)
    { // 010101 per resettare a 1 lo slave ID
        EEPROM.update(1, 1);
        blinky();
        delay(1000);
        reboot();
    }

    if (commands[0] == 0x000A && commands[1] == 0x000A && commands[2] == 0x000A)
    { // 0A0A0A test di blinkn dei led rosso e verde
        blinky();
        delay(1000);
        reboot();
    }

    if (commands[0] == 0x0005 && commands[1] == 0x0005)
    { // 0505: comando per modificare il valore di sensibilità (0 - 2)
        wdt_reset();
        if (commands[2] < 3)
        { // se il valore è maggiore di 2, resetta il valore all'indice 0
            EEPROM.update(SENSITIVITY_EEPROM_LOCATION, commands[2]);
        }
        else
            EEPROM.update(SENSITIVITY_EEPROM_LOCATION, 0);
        blinky();
        delay(1000);
        reboot();
    }

    if (commands[0] == 0x06 && commands[1] == 0x66)
        reboot(); // reset via modbus

    /******************************************************************************************************************************************************/
    wdt_disable();

    if (!digitalRead(MODBUS_BUTTON) && !sensitivityEditMode && !alarm)
    { // ModBus button: se rilasciato prima di 5 secondi, mostra tramite lampeggio lo slave ID, se tenuto premuto entra in modalità modifica dello slave ID
        modbusButtonLastStatus = true;
        timerModbus.resume();
    }
    else
    {
        if (modbusButtonLastStatus && !modbusEditMode)
        {
            /* Lampeggio led per leggere lo slave ID: entrambi i led accesi contano le centinaia,
            il led rosso conta le decine e il led verde le unità*/
            blinky_show();
            uint8_t units = 0;
            uint8_t tens = 0;
            uint8_t hundreds = 0;
            const uint8_t ascii = 48; // costante per convertire i caratteri numerici ASCII in numeri interi
            switch (String(slaveID).length())
            {
            case 1:
                units = String(slaveID).charAt(0);
                break;
            case 2:
                units = String(slaveID).charAt(1);
                tens = String(slaveID).charAt(0);
                break;
            case 3:
                units = String(slaveID).charAt(2);
                tens = String(slaveID).charAt(1);
                hundreds = String(slaveID).charAt(0);
                break;
            }
            if (hundreds > 0)
            {
                for (int i = 0; i < hundreds - ascii; i++)
                {
                    digitalWrite(LED_RED, 0);
                    digitalWrite(LED_GREEN, 0);
                    delay(500);
                    digitalWrite(LED_RED, 1);
                    digitalWrite(LED_GREEN, 1);
                    delay(500);
                }
            }
            if (tens > 0)
            {
                for (int i = 0; i < tens - ascii; i++)
                {
                    digitalWrite(LED_RED, 0);
                    delay(500);
                    digitalWrite(LED_RED, 1);
                    delay(500);
                }
            }
            if (units > 0)
            {
                for (int i = 0; i < units - ascii; i++)
                {
                    digitalWrite(LED_GREEN, 0);
                    delay(500);
                    digitalWrite(LED_GREEN, 1);
                    delay(500);
                }
            }
            delay(100);
        }
        timerModbus.start();
        timerModbus.pause();
        modbusButtonLastStatus = false;
    }

    if (timerModbus.read() >= 5000)
    { // tenere premuto il pulsante ModBus per più di 10 secondi per entrare in modalità modifica
        if (!modbusEditMode && !digitalRead(MODBUS_BUTTON))
        {
            blinky();
            timerReboot.start();
            modbusEditMode = true;
        }
        delay(100);
    }

    if (!digitalRead(MODBUS_BUTTON) && modbusEditMode && !alarm)
    { // EEPROM update
        timerReboot.stop();
        if (editUnits)
        { // premere il pulsante ModBus per modificare le unità dello slave ID, ad ogni pressione corrisponde un incremento, a 9 incrementi riparte da 0
            if (units_ < 9)
                units_++;
            else
                units_ = 0;
            digitalWrite(LED_GREEN, 0);
            delay(100);
            digitalWrite(LED_GREEN, 1);
            delay(100);
        }
        else
        {
            if (tens_ < 22)
                tens_++; // premere il pulsante ModBus per incrementare le decine, da 0 a 22, poi riparte il conto
            else
                tens_ = 0;
            digitalWrite(LED_RED, 0);
            delay(100);
            digitalWrite(LED_RED, 1);
            delay(100);
        }
        slaveID = (tens_ * 10) + units_;
        delay(100);
        timerReboot.start();
    }

    if (!digitalRead(SENSITIVITY_BUTTON) && modbusEditMode && !alarm)
    { // in modalità modifica dello slave ID, passa da modifica delle unità a modifica delle decine e viceversa
        timerReboot.stop();
        shortBlinky();
        editUnits = !editUnits;
        timerReboot.start();
    }

    /* se non vengono toccati bottoni per 10 secondi, il dispositivo si resetta e salva lo slave ID
    se entrati in modalità di modifica non viene toccato alcun pulsante, lo slave ID viene resettato a 1 */
    if (modbusEditMode && timerReboot.read() > 10000)
    {
        if (slaveID == 0 || slaveID == EEPROM.read(MODBUS_SLAVE_ID_EEPROM_LOCATION))
            EEPROM.update(1, 1);
        else
            EEPROM.update(MODBUS_SLAVE_ID_EEPROM_LOCATION, slaveID);
        blinky();
        reboot();
    }

    /* bottone sensibilità (1-3):
      1: solo led rosso
      2: solo led verde
      3: entrambi i leds
  */
    if (!digitalRead(SENSITIVITY_BUTTON) && !modbusEditMode && !alarm)
    {
        sensitivityButtonLastStatus = 1;
        timerSensitivity.resume();
    }
    else
    {
        if (sensitivityButtonLastStatus && !sensitivityEditMode)
        {
            blinky_show();
            show_sensitivity();
        }
        timerSensitivity.start();
        timerSensitivity.pause();
        sensitivityButtonLastStatus = 0;
    }

    if (timerSensitivity.read() >= 5000)
    { // premere per più di 5 secondi il pulsante sensibilità per entrare in modalità modifica della sensibilità
        if (!sensitivityEditMode && !digitalRead(SENSITIVITY_BUTTON))
        {
            timerReboot.start();
            blinky();
            sensitivityEditMode = true;
        }
        delay(100);
    }

    if ((!digitalRead(MODBUS_BUTTON) || !digitalRead(SENSITIVITY_BUTTON)) && sensitivityEditMode && !alarm)
    { // modalità modifica della sensibilità
        timerReboot.stop();
        sensitivityValueIndex++;
        if (sensitivityValueIndex >= 3)
            sensitivityValueIndex = 0;
        show_sensitivity();
        timerReboot.start();
        delay(100);
    }

    // se non vengono premuti bottoni in modalità sensibilità per più di 10 secondi, il dispositivo si riavvia salvando le eventuali modifiche
    if (sensitivityEditMode && timerReboot.read() > 10000)
    {
        EEPROM.update(SENSITIVITY_EEPROM_LOCATION, sensitivityValueIndex);
        blinky();
        reboot();
    }
}

/*********************************************************************************************************/

void firstBoot()
{ // primo avvio, l'EEPROM viene resettata ed è pronta per salvare i parametri
    if (EEPROM[64] != 64)
    {
        EEPROM.update(64, 64);
        EEPROM.update(MODBUS_SLAVE_ID_EEPROM_LOCATION, 1);
        EEPROM.update(SENSITIVITY_EEPROM_LOCATION, 2);
    }
}

void blinky()
{ // funzione lampeggio
    for (int i = 0; i < 20; i++)
    {
        digitalWrite(LED_RED, 0);
        delay(25);
        digitalWrite(LED_RED, 1);
        delay(25);
        digitalWrite(LED_GREEN, 0);
        delay(25);
        digitalWrite(LED_GREEN, 1);
        delay(25);
    }
}

void shortBlinky()
{ // funzione lampeggio corto
    for (int i = 0; i < 10; i++)
    {
        digitalWrite(LED_RED, 0);
        delay(12);
        digitalWrite(LED_RED, 1);
        delay(12);
        digitalWrite(LED_GREEN, 0);
        delay(12);
        digitalWrite(LED_GREEN, 1);
        delay(12);
    }
}

void analog_average(int ch, int AN_ch)
{ // lettura sensori analogici con relativa media, se il sensore non è presente, ritorna un valore fisso
    if (!sensorsPresence[ch])
    {
        Analog_Value[ch] = analogRead(AN_ch);
        delayMicroseconds(1000);
        Analog_Value[ch] = analogRead(AN_ch);
        Analog_Value[ch] = 0;
        delayMicroseconds(1000);

        for (int i = 0; i < 10; i++)
        {
            Analog_Value[ch] += analogRead(AN_ch);
            delayMicroseconds(500);
        }
        Analog_Value[ch] /= 10;
        wdt_reset();
    }
    else
        Analog_Value[ch] = 1024;
}

void blinky_show()
{ // veloce lampeggio prima di mostrare i valori dello slave ID
    digitalWrite(LED_RED, 0);
    delay(10);
    digitalWrite(LED_RED, 1);
    delay(10);
    digitalWrite(LED_GREEN, 0);
    delay(10);
    digitalWrite(LED_GREEN, 1);
    delay(10);
    digitalWrite(LED_RED, 0);
    delay(10);
    digitalWrite(LED_RED, 1);
    delay(10);
}

void show_sensitivity()
{
    switch (sensitivityValueIndex)
    {
    case 0:
        digitalWrite(LED_RED, 0);
        delay(1000);
        digitalWrite(LED_RED, 1);
        break;
    case 1:
        digitalWrite(LED_GREEN, 0);
        delay(1000);
        digitalWrite(LED_GREEN, 1);
        break;
    case 2:
        digitalWrite(LED_RED, 0);
        digitalWrite(LED_GREEN, 0);
        delay(1000);
        digitalWrite(LED_RED, 1);
        digitalWrite(LED_GREEN, 1);
        break;
    }
}

bool check_input_connection(int in)
{
    if (analogRead(in) > NO_INPUT_THRESHOLD)
        return false;
    return true;
}

void check_inputs_state()
{
    if (!check_input_connection(0) && !sensorsPresence[0]) // le connessioni non buone verranno allarmate
        discreteInputs[3] = 1;
    else
        discreteInputs[3] = 0;
    if (!check_input_connection(1) && !sensorsPresence[1])
        discreteInputs[4] = 1;
    else
        discreteInputs[4] = 0;
    if (!check_input_connection(2) && !sensorsPresence[2])
        discreteInputs[5] = 1;
    else
        discreteInputs[5] = 0;

    analog_average(0, ANALOG_IN1);
    delay(5);
    analog_average(1, ANALOG_IN2);
    delay(5);
    analog_average(2, ANALOG_IN3);
    delay(5);

    if (Analog_Value[0] < sensitivityValue)
    {
        discreteInputs[0] = 1;
        discreteInputs[3] = 0;
    }
    else
    {
        if (!sensorsPresence[0])
            discreteInputs[0] = 0;
    }

    if (Analog_Value[1] < sensitivityValue)
    {
        discreteInputs[1] = 1;
        discreteInputs[4] = 0;
    }
    else
    {
        if (!sensorsPresence[1])
            discreteInputs[1] = 0;
    }

    if (Analog_Value[2] < sensitivityValue)
    {
        discreteInputs[2] = 1;
        discreteInputs[5] = 0;
    }
    else
    {
        if (!sensorsPresence[2])
            discreteInputs[2] = 0;
    }
}

void check_inputs_presence()
{
    Wire.begin();
    Wire.beginTransmission(PCA9536D_ADDRESS);
    Wire.write(0x03);       // PC9536D register number 3, pin direction
    Wire.write(0b00000111); // first 3 pin as inputs
    Wire.endTransmission();

    Wire.beginTransmission(PCA9536D_ADDRESS);
    Wire.write(0x00); // asking register number 0 data reading
    Wire.endTransmission();

    Wire.requestFrom(PCA9536D_ADDRESS, 1); // reading pin state..
    if (Wire.available())
        sensorsPresenceByte = Wire.read();

    // fitting sensors presence into the dedicated array
    for (int i = 0; i < 3; i++)
        sensorsPresence[i] = sensorsPresenceByte & (1 << i);
}

void leds_startup()
{
    for (int i = 0; i < 3; i++) // accende i led corrispondenti all'ingresso rilevato
    {

        if (!sensorsPresence[i])
        {
            switch (i)
            {
            case 0:
                digitalWrite(ALARM_LED_1, 0);
                break;
            case 1:
                digitalWrite(ALARM_LED_2, 0);
                break;
            case 2:
                digitalWrite(ALARM_LED_3, 0);
                break;
            }
        }
    }
    delay(1000);
    digitalWrite(ALARM_LED_1, 1);
    digitalWrite(ALARM_LED_2, 1);
    digitalWrite(ALARM_LED_3, 1);
}

/*********************************************************************************************************/