#include <ModbusRTUSlave.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <Timer.h>
#include <avr/wdt.h>

#define DRIVER_ENABLE_PIN 2                // on the EP400v2 that's the control pin
#define RXPIN 10                           // ModBus receiver pin
#define TXPIN 11                           // ModBuse transmission pin
#define LED_RED 9                          // EP400v2's onboard red led
#define LED_GREEN 13                       // EP400v2's onboard green led
#define MODBUS_BUTTON 5                    // EP400v2's ModBus function button pin
#define SENSITIVITY_BUTTON 4               // EP400v2's Sensitivity button pin
#define PERIPHEAL_POWER 8                  // EP400v2's peripheal power
#define MODBUS_SLAVE_ID_EEPROM_LOCATION 1  // ModBus Slave ID EEPROM memory location
#define SENSITIVITY_EEPROM_LOCATION 2      // Sensitivity parameter EEPROM location
#define ANALOG_IN1 A0                      // analog inputs for sensors
#define ANALOG_IN2 A1
#define ANALOG_IN3 A2
const uint8_t inputs[3] = { 2, 3, 4 };  // defining inputs pins

SoftwareSerial mySerial(RXPIN, TXPIN);               // defining SoftwareSerial communication
ModbusRTUSlave modbus(mySerial, DRIVER_ENABLE_PIN);  // defining modbus rtu slave
Timer modbusTimer;                                   // timer for Slave ID edit mode
Timer modbusTimerReboot;                             // timer for Slave ID edit mode reboot
Timer sensorsTimer;                                  // timer for analog sensors

/* defining ModBus inputs, registers.. */
bool discreteInputs[3];
bool coils[2];
uint16_t commands[3];

uint8_t slaveID;                             // slave ID for ModBus, the default is 1
int sensitivityValue;                        // sensitivity value
uint8_t sensitivityValueIndex = 0;           // sensitivity value index
bool modbusEditMode = false;                 // edit mode control variable
bool modbusButtonLastStatus;                 // control variable for ModBus button release
int Analog_Value[3] = { 1024, 1024, 1024 };  // analog values for sensors
int sensorsInterval = 4000;                  // time interval for sensors (mss)
int sensitivityConsts[3] = { 350, 500, 650 };
bool alarm = false;
bool alarmSupport = false;

/*********************************************************************************************************/

void (*reboot)(void) = 0;  // reset function, at address 0, calling it will make the board reboot

void firstBoot() {  // first boot function, to reset memory location in which is stored the slave ID
  if (EEPROM[64] != 64) {
    EEPROM.update(64, 64);
    EEPROM.update(MODBUS_SLAVE_ID_EEPROM_LOCATION, 1);
    EEPROM.update(SENSITIVITY_EEPROM_LOCATION, 1);
  }
}

void blinky() {  // led blink function
  for (int i = 0; i < 20; i++) {
    digitalWrite(LED_RED, HIGH);
    delay(25);
    digitalWrite(LED_RED, LOW);
    delay(25);
    digitalWrite(LED_GREEN, HIGH);
    delay(25);
    digitalWrite(LED_GREEN, LOW);
    delay(25);
  }
}

void shortBlinky() {  // short led blink function
  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_RED, HIGH);
    delay(12);
    digitalWrite(LED_RED, LOW);
    delay(12);
    digitalWrite(LED_GREEN, HIGH);
    delay(12);
    digitalWrite(LED_GREEN, LOW);
    delay(12);
  }
}

void analog_average(int ch, int AN_ch) {  // Lettura canali analogici
  Analog_Value[ch] = analogRead(AN_ch);
  delayMicroseconds(1000);
  Analog_Value[ch] = analogRead(AN_ch);
  Analog_Value[ch] = 0;
  delayMicroseconds(1000);

  for (int i = 0; i < 10; i++) {
    Analog_Value[ch] += analogRead(AN_ch);
    delayMicroseconds(500);
  }
  Analog_Value[ch] /= 10;
  // wdt_reset();
}

/*********************************************************************************************************/

void setup() {
  Serial.begin(9600);  //debug
  firstBoot();
  wdt_disable();  // watchdog at bay

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

  modbus.configureDiscreteInputs(discreteInputs, 3);  // bool array of discrete inputs values, number of discrete inputs
  modbus.configureHoldingRegisters(commands, 3);      // first two numbers are used for commands, third is the parameter
  modbus.begin(slaveID, 9600);

  modbusTimer.start();               // starting timer..
  sensorsTimer.start();              // starting sensors timer
  digitalWrite(PERIPHEAL_POWER, 1);  // peripheal power
  wdt_enable(WDTO_1S);               // watchdog enabled, on duty
}

void loop() {
  // every 10 seconds, sensor are gathering data:
  wdt_reset();
  if (sensorsTimer.read() % sensorsInterval == 0 && !modbusEditMode) {
    analog_average(0, ANALOG_IN1);
    delay(5);
    analog_average(1, ANALOG_IN2);
    delay(5);
    analog_average(2, ANALOG_IN3);
    delay(5);
    if (Analog_Value[0] > sensitivityValue && Analog_Value[1] > sensitivityValue && Analog_Value[2] > sensitivityValue) {  // everything OK
      digitalWrite(LED_GREEN, 1);
      delay(500);
      digitalWrite(LED_GREEN, 0);
      discreteInputs[0] = 0;
      discreteInputs[1] = 0;
      discreteInputs[2] = 0;
      alarmSupport = false;
      alarm = false;
    } else {  // ALARM!
      //alarm = true;
      if (!alarmSupport) {
        if (Analog_Value[0] < sensitivityValue) discreteInputs[0] = 1;
        if (Analog_Value[1] < sensitivityValue) discreteInputs[1] = 1;
        if (Analog_Value[2] < sensitivityValue) discreteInputs[2] = 1;
        alarm = true;
      }
    }
  }
  wdt_disable();

  modbus.poll();  // update ModBus values

  if (commands[0] == 0x00C0 && commands[1] == 0x0050) {  // C050, is the ModBus master's command to update slave ID (1 - 229)
    wdt_reset();
    if (slaveID < 231) {  // if the value is too high, reset the slave ID to 1
      EEPROM.update(MODBUS_SLAVE_ID_EEPROM_LOCATION, commands[2]);
    } else EEPROM.update(MODBUS_SLAVE_ID_EEPROM_LOCATION, 1);
    blinky();
    delay(1000);
    reboot();
  }
  wdt_disable();

  if (commands[0] == 0x0001 && commands[1] == 0x0001 && commands[2] == 0x0001) {  // 010101 Slave ID reset command
    wdt_reset();
    EEPROM.update(MODBUS_SLAVE_ID_EEPROM_LOCATION, 1);
    blinky();
    delay(1000);
    reboot();
  }
  wdt_disable();

  if (commands[0] == 0x000A && commands[1] == 0x000A && commands[2] == 0x000A) {  // 0A0A0A red and green led blink test
    wdt_reset();
    blinky();
    delay(1000);
    reboot();
  }
  wdt_disable();

  if (commands[0] == 0x0005 && commands[1] == 0x0005) {  // 0505: the ModBus command to update sensitivity value (0 - 3)
    wdt_reset();
    if (commands[2] < 3) {  // if the value is too high, reset sensitivity level to 0
      EEPROM.update(SENSITIVITY_EEPROM_LOCATION, commands[2]);
    } else EEPROM.update(SENSITIVITY_EEPROM_LOCATION, 0);
    blinky();
    delay(1000);
    reboot();
  }
  wdt_disable();

  if (!digitalRead(MODBUS_BUTTON) && !alarm) {  // ModBus button: if released shows on leds ModBus Slave ID, if 5-seconds long press enters Slave ID edit mode
    modbusButtonLastStatus = true;
    modbusTimer.resume();
  } else {
    if (modbusButtonLastStatus && !modbusEditMode) {
      /* Slave ID monitor on leds (release ModBus button):
      Red leds are the units count, green leds are the tens, both leds are for the hundreds */
      wdt_reset();
      shortBlinky();
      delay(500);
      uint8_t units = 0;
      uint8_t tens = 0;
      uint8_t hundreds = 0;
      uint8_t ascii = 48;  // ASCII conversion number
      switch (String(slaveID).length()) {
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
      if (hundreds > 0) {
        for (int i = 0; i < hundreds - ascii; i++) {
          digitalWrite(LED_RED, 1);
          digitalWrite(LED_GREEN, 1);
          delay(500);
          digitalWrite(LED_RED, 0);
          digitalWrite(LED_GREEN, 0);
          delay(500);
        }
      }
      if (tens > 0) {
        for (int i = 0; i < tens - ascii; i++) {
          digitalWrite(LED_RED, 1);
          delay(500);
          digitalWrite(LED_RED, 0);
          delay(500);
        }
      }
      if (units > 0) {
        for (int i = 0; i < units - ascii; i++) {
          digitalWrite(LED_GREEN, 1);
          delay(500);
          digitalWrite(LED_GREEN, 0);
          delay(500);
        }
      }
      delay(100);
    }
    modbusTimer.pause();
    modbusButtonLastStatus = false;
  }
  wdt_disable();

  wdt_reset();
  if (modbusTimer.read() % 5000 == 0 && modbusTimer.read() >= 5000) {  // long press ModBus button for 5 seconds to enter edit mode
    if (!modbusEditMode && !digitalRead(MODBUS_BUTTON)) {
      modbusTimerReboot.start();
      blinky();
      modbusEditMode = true;
    }
    delay(100);
  }
  wdt_disable();

  if (!digitalRead(MODBUS_BUTTON) && modbusEditMode && !alarm) {  // EEPROM update condition
    modbusTimerReboot.stop();
    if (slaveID < 231) {
      slaveID++;
      EEPROM.update(MODBUS_SLAVE_ID_EEPROM_LOCATION, slaveID);  // updating the EEPROM...
    } else slaveID = 1;
    shortBlinky();
    modbusTimerReboot.start();
    delay(100);
  }

  /* if Slave ID edit mode is idle for 10 seconds, board reboots
  if Slave ID is unchanged, value will be reseto to 1 */
  if (modbusEditMode && modbusTimerReboot.read() == 10000) {
    if (slaveID == 0) EEPROM.update(MODBUS_SLAVE_ID_EEPROM_LOCATION, 1);
    else EEPROM.update(MODBUS_SLAVE_ID_EEPROM_LOCATION, slaveID);
    blinky();
    reboot();
  }

  /* sensitivity button (1-3):
    1: red led only
    2: green led only
    3: both green and red leds
  */
  wdt_reset();
  if (!digitalRead(SENSITIVITY_BUTTON) && !modbusEditMode && !alarm) {
    shortBlinky();
    delay(100);
    if (sensitivityValueIndex < 2) ++sensitivityValueIndex;
    else sensitivityValueIndex = 0;
    EEPROM.update(SENSITIVITY_EEPROM_LOCATION, sensitivityValueIndex);
    sensitivityValue = sensitivityConsts[sensitivityValueIndex];

    switch (sensitivityValueIndex) {
      case 0:
        digitalWrite(LED_RED, 1);
        delay(1000);
        digitalWrite(LED_RED, 0);
        break;
      case 1:
        digitalWrite(LED_GREEN, 1);
        delay(1000);
        digitalWrite(LED_GREEN, 0);
        break;
      case 2:
        digitalWrite(LED_RED, 1);
        digitalWrite(LED_GREEN, 1);
        delay(1000);
        digitalWrite(LED_RED, 0);
        digitalWrite(LED_GREEN, 0);
        break;
    }
  }
  wdt_disable();

  wdt_reset();
  if (alarm && ((sensorsTimer.read() % sensorsInterval == 0) || !digitalRead(MODBUS_BUTTON) || !digitalRead(SENSITIVITY_BUTTON))) {  // ALARM led disable
    alarm = false;
    alarmSupport = true;

    discreteInputs[0] = 0;
    discreteInputs[1] = 0;
    discreteInputs[2] = 0;
    modbus.poll();

    delay(300);
  }
  wdt_disable();

  if (alarm) {  // ALARM led blink
    wdt_reset();
    digitalWrite(LED_RED, 1);
    delay(150);
    digitalWrite(LED_RED, 0);
    delay(150);
  }
  wdt_disable();
}