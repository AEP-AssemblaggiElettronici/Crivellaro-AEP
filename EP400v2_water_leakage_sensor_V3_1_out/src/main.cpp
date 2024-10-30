#include <Arduino.h>
#include <ModbusRTUSlave.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <Timer.h>
#include <avr/wdt.h>

#define DRIVER_ENABLE_PIN 2               // on the EP400v2 that's the control pin
#define RXPIN 10                          // ModBus receiver pin
#define TXPIN 11                          // ModBuse transmission pin
#define LED_RED 13                        // EP400v2's onboard red led
#define LED_GREEN 17                      // EP400v2's onboard green led
#define MODBUS_BUTTON 8                   // EP400v2's ModBus function button pin
#define SENSITIVITY_BUTTON 9              // EP400v2's Sensitivity button pin
// #define PERIPHEAL_POWER 8                 // EP400v2's peripheal power
#define MODBUS_SLAVE_ID_EEPROM_LOCATION 1 // ModBus Slave ID EEPROM memory location
#define SENSITIVITY_EEPROM_LOCATION 2     // Sensitivity parameter EEPROM location
#define ANALOG_IN1 A0                     // analog inputs for sensors
#define ANALOG_IN2 A1
#define ANALOG_IN3 A2
#define ALARM_PIN 7
#define ALARM_LED_1 3
#define ALARM_LED_2 4
#define ALARM_LED_3 5
const uint8_t inputs[3] = {2, 3, 4}; // defining inputs pins

SoftwareSerial mySerial(RXPIN, TXPIN);              // defining SoftwareSerial communication
ModbusRTUSlave modbus(mySerial, DRIVER_ENABLE_PIN); // defining modbus rtu slave
Timer timerModbus;                                  // timer for Slave ID edit mode
Timer timerSensitivity;
Timer timerReboot;  // timer for Slave ID edit mode reboot
Timer sensorsTimer; // timer for analog sensors

/* defining ModBus inputs, registers.. */
bool discreteInputs[3];
uint16_t commands[3];

uint8_t slaveID;                          // slave ID for ModBus, the default is 1
int sensitivityValue;                     // sensitivity value
uint8_t sensitivityValueIndex = 0;        // sensitivity value index
bool modbusEditMode = false;              // edit mode control variable
bool modbusButtonLastStatus;              // control variable for ModBus button release
bool sensitivityEditMode = false;         // sensitivity mode control variable
bool sensitivityButtonLastStatus;         // control variable for sensitivity button release
int Analog_Value[3] = {1024, 1024, 1024}; // analog values for sensors
int sensorsInterval = 4000;               // time interval for sensors (mss)
int sensitivityConsts[3] = {350, 500, 650};
bool alarm = false;
bool alarmSupport = false;
uint8_t units_ = 0;    // unit digit variable for new ModBus slave ID
uint8_t tens_ = 0;     // tens digit variable for new ModBus slave ID
bool editUnits = true; // toggle variable to edit new ModBus slave ID via buttons

/*********************************************************************************************************/

void (*reboot)() = 0; // reset function, at address 0, calling it will make the board reboot

void firstBoot()
{ // first boot function, to reset memory location in which is stored the slave ID
    if (EEPROM[64] != 64)
    {
        EEPROM.update(64, 64);
        EEPROM.update(MODBUS_SLAVE_ID_EEPROM_LOCATION, 1);
        EEPROM.update(SENSITIVITY_EEPROM_LOCATION, 1);
    }
}

void blinky()
{ // led blink function
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
{ // short led blink function
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
{ // Reading analog sensors'
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

void blinky_show()
{ // small blink before showing on leds Modbus ID or sensitivity values
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

void check_inputs_state()
{
    analog_average(0, ANALOG_IN1);
    delay(5);
    analog_average(1, ANALOG_IN2);
    delay(5);
    analog_average(2, ANALOG_IN3);
    delay(5);

    if (Analog_Value[0] < sensitivityValue)
        discreteInputs[0] = 1;
    else
        discreteInputs[0] = 0;
    if (Analog_Value[1] < sensitivityValue)
        discreteInputs[1] = 1;
    else
        discreteInputs[1] = 0;
    if (Analog_Value[2] < sensitivityValue)
        discreteInputs[2] = 1;
    else
        discreteInputs[2] = 0;
}

/*********************************************************************************************************/

void setup()
{
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

    modbus.configureDiscreteInputs(discreteInputs, 3); // bool array of discrete inputs values, number of discrete inputs
    modbus.configureHoldingRegisters(commands, 3);     // first two numbers are used for commands, third is the parameter
    modbus.begin(slaveID, 9600);

    timerModbus.start(); // starting hold buttons timer
    timerSensitivity.start();
    sensorsTimer.start();             // starting sensors timer
    // digitalWrite(PERIPHEAL_POWER, 1); // peripheal power

    delay(100);
}

void loop()
{
    // sensors gathering data (every 4 seconds)
    if (sensorsTimer.read() % sensorsInterval < 500)
        check_inputs_state();

    // Hypothetical alarm led blink (blinks only if sensors are triggered):
    if (millis() % 500 < 250)
    {
        if (discreteInputs[0])
            digitalWrite(ALARM_LED_1, 0);
        else
            digitalWrite(ALARM_LED_1, 1);
        if (discreteInputs[1])
            digitalWrite(ALARM_LED_2, 0);
        else
            digitalWrite(ALARM_LED_2, 1);
        if (discreteInputs[2])
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

    // sensors are evalutated:
    if (
        Analog_Value[0] > sensitivityValue &&
        Analog_Value[1] > sensitivityValue &&
        Analog_Value[2] > sensitivityValue)
    { // everything OK
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
    else // ALARM!
    {
        alarm = true;
        digitalWrite(ALARM_PIN, 0);
        digitalWrite(LED_GREEN, 1);
    }

    modbus.poll();

    //***************************************************MODUBS COMMANDS: ***************************************************************************/

    if (commands[0] == 0x00C0 && commands[1] == 0x0050)
    { // C050, is the ModBus master's command to update slave ID
        Serial.println(commands[2]);
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
    { // 010101 Slave ID reset command
        EEPROM.update(1, 1);
        blinky();
        delay(1000);
        reboot();
    }

    if (commands[0] == 0x000A && commands[1] == 0x000A && commands[2] == 0x000A)
    { // 0A0A0A red and green led blink test
        blinky();
        delay(1000);
        reboot();
    }

    if (commands[0] == 0x0005 && commands[1] == 0x0005)
    { // 0505: the ModBus command to update sensitivity value (0 - 3)
        wdt_reset();
        if (commands[2] < 3)
        { // if the value is too high, reset sensitivity level to 0
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
    { // ModBus button: if released shows on leds ModBus Slave ID, if 5-seconds long press enters Slave ID edit mode
        modbusButtonLastStatus = true;
        timerModbus.resume();
    }
    else
    {
        if (modbusButtonLastStatus && !modbusEditMode)
        {
            /* Slave ID monitor on leds (release ModBus button):
            Red leds are the units count, green leds are the tens, both leds are for the hundreds */
            blinky_show();
            uint8_t units = 0;
            uint8_t tens = 0;
            uint8_t hundreds = 0;
            uint8_t ascii = 48; // ASCII conversion number
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
    { // long press ModBus button for 5 seconds to enter edit mode
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
        { // press ModBus button to edit units on the slave ID - 0 to 9 and over
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
                tens_++; // press ModBus button to edit tens on the slave ID - 0 to 22 and over
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
    { // on slave ID edit mode, toggles between tens and unit edit
        timerReboot.stop();
        shortBlinky();
        editUnits = !editUnits;
        timerReboot.start();
    }

    /* if Slave ID edit mode is idle for 10 seconds, board reboots
    if Slave ID is unchanged, value will be reseto to 1 */
    if (modbusEditMode && timerReboot.read() > 10000)
    {
        if (slaveID == 0 || slaveID == EEPROM.read(MODBUS_SLAVE_ID_EEPROM_LOCATION))
            EEPROM.update(1, 1);
        else
            EEPROM.update(MODBUS_SLAVE_ID_EEPROM_LOCATION, slaveID);
        blinky();
        reboot();
    }

    /* sensitivity button (1-3):
      1: red led only
      2: green led only
      3: both green and red leds
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
    { // long press Sensitivity button for 5 seconds to enter edit mode
        if (!sensitivityEditMode && !digitalRead(SENSITIVITY_BUTTON))
        {
            timerReboot.start();
            blinky();
            sensitivityEditMode = true;
        }
        delay(100);
    }

    if ((!digitalRead(MODBUS_BUTTON) || !digitalRead(SENSITIVITY_BUTTON)) && sensitivityEditMode && !alarm)
    { // sensitivity edit mode
        timerReboot.stop();
        sensitivityValueIndex++;
        if (sensitivityValueIndex >= 3)
            sensitivityValueIndex = 0;
        show_sensitivity();
        timerReboot.start();
        delay(100);
    }

    // if sensitivity edit mode is idle for 10 seconds, board reboots
    if (sensitivityEditMode && timerReboot.read() > 10000)
    {
        EEPROM.update(SENSITIVITY_EEPROM_LOCATION, sensitivityValueIndex);
        blinky();
        reboot();
    }
}