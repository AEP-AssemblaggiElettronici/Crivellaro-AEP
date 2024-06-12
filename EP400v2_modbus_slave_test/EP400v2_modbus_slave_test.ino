#include <ModbusRTUSlave.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <Timer.h>

#define DRIVER_ENABLE_PIN 2             // on the EP400v2 that's the control pin
#define RXPIN 10                        // ModBus receiver pin
#define TXPIN 11                        // ModBuse transmission pin
#define LED_RED 13                      // EP400v2's onboard red led
#define LED_GREEN 9                     // EP400v2's onboard green led
#define MODBUS_BUTTON 5                 // EP400v2's ModBus function button pin
#define SENSITIVITY_BUTTON 6            // EP400v2's Sensitivity button pin
#define PERIPHEAL_POWER 8               // EP400v2's peripheal power
const uint8_t inputs[3] = { 2, 3, 4 };  // defining inputs pins !!!CHIEDERE SE SU QUESTA SCHEDA SONO CAMBIATI!!!

SoftwareSerial mySerial(RXPIN, TXPIN);               // defining SoftwareSerial communication
ModbusRTUSlave modbus(mySerial, DRIVER_ENABLE_PIN);  // defining modbus rtu slave
Timer timer;                                         // timer for Slave ID edit mode
Timer timerReboot;                                   // timer for Slave ID edit mode reboot

/* defining ModBus inputs, registers.. */
bool discreteInputs[3];
bool coils[2];
uint16_t commands[3];

uint8_t slaveID;              // slave ID for ModBus, the default is 1
uint8_t previousSlaveID;      // slave ID for ModBus, cache
bool modbusEditMode = false;  // edit mode control variable

/*********************************************************************************************************/

void (*reboot)(void) = 0;  // reset function, at address 0, calling it will make the board reboot

void firstBoot() {  // first boot function, to reset memory location in which is stored the slave ID
  if (EEPROM[64] != 64) {
    EEPROM.update(64, 64);
    EEPROM.update(1, 1);
  } else {
    EEPROM.update(63, 1);
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

/*********************************************************************************************************/

void setup() {
  firstBoot();

  slaveID = EEPROM.read(1);
  previousSlaveID = EEPROM.read(1);

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

  timer.start();                     // starting timer..
  digitalWrite(PERIPHEAL_POWER, 1);  // peripheal power
}

void loop() {
  discreteInputs[0] = !digitalRead(inputs[0]);  // putting inputs pin data into ModBus variables, digital values are inverted
  discreteInputs[1] = !digitalRead(inputs[1]);
  discreteInputs[2] = !digitalRead(inputs[2]);

  modbus.poll();

  if (commands[0] == 0x00C0 && commands[1] == 0x0050) {  // C050, is the ModBus master's command to update slave ID
    if (slaveID < 100) {
      EEPROM.update(1, commands[2]);
    } else EEPROM.update(1, 1);
    blinky();
    delay(1000);
    reboot();
  }

  if (commands[0] == 0x0001 && commands[1] == 0x0001 && commands[2] == 0x0001) {  // Slave ID reset command
    EEPROM.update(1, 1);
    blinky();
    delay(1000);
    reboot();
  }

  if (commands[0] == 0x000A && commands[1] == 0x000A && commands[2] == 0x000A) {
    blinky();
    delay(1000);
    reboot();
  }

  /* Edit Mode BUTTON routine: if the BUTTON is pressed for at least 5 seconds, board enters slave ID edit mode
     in edit mode, use the BUTTON to increment slave ID by 1 (from 1 to 99)
     wait for 10 seconds to reset the board and apply changes
  */
  if (!digitalRead(MODBUS_BUTTON) || !digitalRead(SENSITIVITY_BUTTON)) {  // ModBus button timer check
    timer.resume();
  } else {
    timer.pause();
  }

  if (timer.read() % 5000 == 0 && timer.read() >= 5000) {  // long press ModBus button for 5 seconds to enter edit mode
    delay(100);
    if (!modbusEditMode && !digitalRead(MODBUS_BUTTON)) {
      timerReboot.start();
      modbusEditMode = true;
      blinky();
    }
  }

  if (!digitalRead(MODBUS_BUTTON) && modbusEditMode) {
    timerReboot.stop();
    if (slaveID < 100) {
      slaveID++;
      EEPROM.update(1, slaveID);  // updating the EEPROM...
    } else EEPROM.update(1, 1);
    shortBlinky();
    timerReboot.start();
  }

  /* if Slave ID edit mode is idle for 10 seconds, board reboots
  if Slave ID is unchanged, value will be reseto to 1 */
  if (modbusEditMode && timerReboot.read() == 10000) {
    if (slaveID == previousSlaveID) EEPROM.update(1, 1);
    blinky();
    reboot();
  }
}