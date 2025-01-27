#include <Arduino.h>
#include <ModbusRTUSlave.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <Timer.h>
#include <avr/wdt.h>
#include <Wire.h>

#define DRIVER_ENABLE_PIN 2               // on the EP400v2 that's the control pin
#define RXPIN 10                          // ModBus receiver pin
#define TXPIN 11                          // ModBuse transmission pin
#define LED_RED 13                        // EP400v2's onboard red led
#define LED_GREEN A3                      // EP400v2's onboard green led
#define MODBUS_BUTTON 8                   // EP400v2's ModBus function button pin
#define SENSITIVITY_BUTTON 9              // EP400v2's Sensitivity button pin
#define MODBUS_SLAVE_ID_EEPROM_LOCATION 1 // ModBus Slave ID EEPROM memory location
#define SENSITIVITY_EEPROM_LOCATION 2     // Sensitivity parameter EEPROM location
#define MODE_EEPROM_LOCATION 3
#define ANALOG_IN1 A0 // analog inputs for sensors
#define ANALOG_IN2 A1
#define ANALOG_IN3 A2
#define ALARM_PIN 7
#define A 3 // A, B and C are MC14051BDTR2G commutator pins'
#define B 4
#define C 5
#define PCA9535PW_ADDRESS 0x27
#define BUZZER 6
#define IN_H 12
#define NO_INPUT_THRESHOLD 1010

SoftwareSerial mySerial(RXPIN, TXPIN);              // defining SoftwareSerial communication
ModbusRTUSlave modbus(mySerial, DRIVER_ENABLE_PIN); // defining modbus rtu slave
Timer timerModbus;                                  // timer for Slave ID edit mode
Timer timerSensitivity;
Timer timerReboot;  // timer for Slave ID edit mode reboot
Timer sensorsTimer; // timer for analog sensors

/* defining ModBus inputs, registers.. */
bool discreteInputs[20]; // first 10 are input alarm values, the other 10 checks input's connection (1: ok, 0: disconnected)
uint16_t commands[3];

uint8_t slaveID;                          // slave ID for ModBus, the default is 1
int sensitivityValue;                     // sensitivity value
uint8_t sensitivityValueIndex = 0;        // sensitivity value index
bool modbusEditMode = false;              // edit mode control variable
bool modbusButtonLastStatus;              // control variable for ModBus button release
bool sensitivityEditMode = false;         // sensitivity mode control variable
bool sensitivityButtonLastStatus;         // control variable for sensitivity button release
int Analog_Value[3] = {1024, 1024, 1024}; // analog values for sensors
int sensitivityConsts[3] = {270, 400, 50<0};
int sensorsInterval = 4000; // time interval for sensors (mss)
bool alarm = false;
uint8_t units_ = 0;    // unit digit variable for new ModBus slave ID
uint8_t tens_ = 0;     // tens digit variable for new ModBus slave ID
bool editUnits = true; // toggle variable to edit new ModBus slave ID via buttons
int binComm = 0b0000;  // binary commutator, to scroll the MC14051BDTR2G gizmo
byte outputAlarm[2] = {0b11111111, 0b11111111};
bool mode;                // '0' is for 10 inputs mode, '1' is for 5 inputs mode

/*********************************************************************************************************/

void (*reboot)() = 0; // reset function, at address 0, calling it will make the board reboot

void firstBoot()
{ // first boot function, to reset memory location in which is stored the slave ID
    if (EEPROM[64] != 64)
    {
        EEPROM.update(64, 64);
        EEPROM.update(MODBUS_SLAVE_ID_EEPROM_LOCATION, 1);
        EEPROM.update(SENSITIVITY_EEPROM_LOCATION, 1);
        EEPROM.update(MODE_EEPROM_LOCATION, 1);
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

bool check_input_connection(int in) // nomen omen
{
    if (analogRead(in) > NO_INPUT_THRESHOLD)
        return false;
    return true;
}

void check_inputs_state()
{
    if (!check_input_connection(0)) // bad connections will be written in discrete inputs
        discreteInputs[10] = 1;
    else
        discreteInputs[10] = 0;
    if (!check_input_connection(1))
        discreteInputs[11] = 1;
    else
        discreteInputs[11] = 0;

    analog_average(0, ANALOG_IN1);
    delay(5);
    analog_average(1, ANALOG_IN2);
    delay(5);

    if (Analog_Value[0] < sensitivityValue)
    {
        discreteInputs[0] = 1;
        discreteInputs[10] = 0;
    }
    else
        discreteInputs[0] = 0;
    if (Analog_Value[1] < sensitivityValue)
    {
        discreteInputs[1] = 1;
        discreteInputs[11] = 0;
    }
    else
        discreteInputs[1] = 0;

    binComm = 0b0000;
    while (binComm < 0b1000)
    {
        digitalWrite(A, (binComm >> 0) & 1); // bitRead(number, little-endian position)
        digitalWrite(B, (binComm >> 1) & 1); // or, better, using plain binary arithmetic: [((var >> bitPosition) & '1' bit]
        digitalWrite(C, (binComm >> 2) & 1);
        analog_average(2, ANALOG_IN3);
        delay(5);

        unsigned short int position = 0; // remapping sensor positions
        switch (binComm)
        {
        case 0b000:
            position = 2;
            break;
        case 0b001:
            position = 3;
            break;
        case 0b010:
            position = 4;
            break;
        case 0b011:
            position = 9;
            break;
        case 0b100:
            position = 8;
            break;
        case 0b101:
            position = 7;
            break;
        case 0b110:
            position = 6;
            break;
        case 0b111:
            position = 5;
            break;
        }

        if (!check_input_connection(2))
            discreteInputs[position + 10] = 1;
        else
            discreteInputs[position + 10] = 0;

        if (Analog_Value[2] < sensitivityValue)
        {
            discreteInputs[position] = 1;
            discreteInputs[position + 10] = 0;
        }
        else
            discreteInputs[position] = 0;

        binComm++;
    }
    if (mode) // if in 5 inputs mode, sets to 0 unused ModBus input check locations
        for (int i = 15; i < 20; i++)
            discreteInputs[i] = 0;
}

/*********************************************************************************************************/

void setup()
{
    firstBoot();

    slaveID = EEPROM.read(MODBUS_SLAVE_ID_EEPROM_LOCATION);
    sensitivityValueIndex = EEPROM.read(SENSITIVITY_EEPROM_LOCATION);
    sensitivityValue = sensitivityConsts[sensitivityValueIndex];
    mode = EEPROM.read(MODE_EEPROM_LOCATION);

    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(MODBUS_BUTTON, INPUT);
    pinMode(SENSITIVITY_BUTTON, INPUT);
    pinMode(ALARM_PIN, OUTPUT);
    pinMode(A, OUTPUT);
    pinMode(B, OUTPUT);
    pinMode(C, OUTPUT);
    pinMode(BUZZER, OUTPUT);
    pinMode(IN_H, OUTPUT);

    digitalWrite(IN_H, 0); // IN_H CMC140518 always low!
    digitalWrite(LED_GREEN, 1);
    digitalWrite(LED_RED, 1);

    modbus.configureDiscreteInputs(discreteInputs, 20); // bool array of discrete inputs values, number of discrete inputs
    modbus.configureHoldingRegisters(commands, 3);      // first two numbers are used for commands, third is the parameter
    modbus.begin(slaveID, 9600);

    Wire.begin();
    Wire.setClock(400000); // Speed up the I²C bus
    Wire.beginTransmission(PCA9535PW_ADDRESS);
    Wire.write(0x06); // Register's config address (0x06 on PCA9535)
    Wire.write(0x00); // Set inputs (1) and outputs (0) pins
    Wire.write(0x00);
    Wire.endTransmission();

    timerModbus.start(); // starting hold buttons timer
    timerSensitivity.start();
    sensorsTimer.start();

    delay(100);
    Serial.begin(9600); // DEBUG
}

void loop()
{
    // sensors gathering data (every 4 seconds)
    if (sensorsTimer.read() % sensorsInterval < 500)
        check_inputs_state();

    // Continous alarm check:
    if (!mode)
    {
        if (discreteInputs[0] == 1 || discreteInputs[10] == 1)
            outputAlarm[1] &= ~(1 << 4); // Writes '0' bit at X position
        else
            outputAlarm[1] |= (1 << 4); // Writes '1' bit at X position
        if (discreteInputs[1] == 1 || discreteInputs[11] == 1)
            outputAlarm[1] &= ~(1 << 3);
        else
            outputAlarm[1] |= (1 << 3);
        if (discreteInputs[2] == 1 || discreteInputs[12] == 1)
            outputAlarm[0] &= ~(1 << 0);
        else
            outputAlarm[0] |= (1 << 0);
        if (discreteInputs[3] == 1 || discreteInputs[13] == 1)
            outputAlarm[0] &= ~(1 << 1);
        else
            outputAlarm[0] |= (1 << 1);
        if (discreteInputs[4] == 1 || discreteInputs[14] == 1)
            outputAlarm[0] &= ~(1 << 2);
        else
            outputAlarm[0] |= (1 << 2);
        if (discreteInputs[5] == 1 || discreteInputs[15] == 1)
            outputAlarm[0] &= ~(1 << 3);
        else
            outputAlarm[0] |= (1 << 3);
        if (discreteInputs[6] == 1 || discreteInputs[16] == 1)
            outputAlarm[0] &= ~(1 << 4);
        else
            outputAlarm[0] |= (1 << 4);
        if (discreteInputs[7] == 1 || discreteInputs[17] == 1)
            outputAlarm[1] &= ~(1 << 0);
        else
            outputAlarm[1] |= (1 << 0);
        if (discreteInputs[8] == 1 || discreteInputs[18] == 1)
            outputAlarm[1] &= ~(1 << 1);
        else
            outputAlarm[1] |= (1 << 1);
        if (discreteInputs[9] == 1 || discreteInputs[19] == 1)
            outputAlarm[1] &= ~(1 << 2);
        else
            outputAlarm[1] |= (1 << 2);
    }
    else
    {
        if (discreteInputs[0] == 1 || discreteInputs[10] == 1) // 0
            outputAlarm[0] &= ~(1 << 0);
        else
            outputAlarm[0] |= (1 << 0);
        if (discreteInputs[1] == 1 || discreteInputs[11] == 1) // 1
            outputAlarm[0] &= ~(1 << 1);
        else
            outputAlarm[0] |= (1 << 1);
        if (discreteInputs[2] == 1 || discreteInputs[12] == 1) // 2
            outputAlarm[0] &= ~(1 << 2);
        else
            outputAlarm[0] |= (1 << 2);
        if (discreteInputs[3] == 1 || discreteInputs[13] == 1) // 3
            outputAlarm[0] &= ~(1 << 3);
        else
            outputAlarm[0] |= (1 << 3);
        if (discreteInputs[4] == 1 || discreteInputs[14] == 1) // 4
            outputAlarm[0] &= ~(1 << 4);
        else
            outputAlarm[0] |= (1 << 4);
    }

    // Hypothetical alarm led blink (blinks only if sensors are triggered):
    Wire.beginTransmission(PCA9535PW_ADDRESS);
    Wire.write(0x02);
    if (millis() % 500 < 250)
    {
        Wire.write(outputAlarm[0]);
        Wire.write(outputAlarm[1]);
        if (alarm)
            digitalWrite(LED_RED, 0);
    }
    else
    {
        Wire.write(0b11111111);
        Wire.write(0b11111111);
        digitalWrite(LED_RED, 1);
    }
    Wire.endTransmission();

    if (outputAlarm[0] == 0b11111111 && // Everything is OK
        outputAlarm[1] == 0b11111111)
    {
        alarm = false;
        digitalWrite(ALARM_PIN, 1);
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
ì        if (commands[2] < 0xE6)
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
        reboot(); // 0666: reset via modbus

    if (commands[0] == 0x33 && commands[1] == 0x33)
    { // 3333: the ModBus command to update 10 inputs or 5 inputs mode ('0' for 10, '1' for 5)
        if (commands[2] == 0)
            EEPROM.update(MODE_EEPROM_LOCATION, 0);
        if (commands[2] == 1)
            EEPROM.update(MODE_EEPROM_LOCATION, 1);
        blinky();
        delay(1000);
        reboot();
    }

    /******************************************************************************************************************************************************/
    wdt_disable();

    if (!digitalRead(MODBUS_BUTTON) && !sensitivityEditMode && !alarm)
    { // ModBus button: if released shows on leds ModBus Slave ID, if 5-seconds long press enters Slave ID edit mode
        delay(50);
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
        delay(50);
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
        delay(50);
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
        delay(50);
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
            delay(50);
            timerReboot.start();
            blinky();
            sensitivityEditMode = true;
        }
        delay(100);
    }

    if ((!digitalRead(MODBUS_BUTTON) || !digitalRead(SENSITIVITY_BUTTON)) && sensitivityEditMode && !alarm)
    { // sensitivity edit mode
        delay(50);
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