#include <ModbusRTUSlave.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

const uint8_t driverEnablePin = 2;  // on the EP400v2 that's the control pin
const uint8_t rxPin = 10;
const uint8_t txPin = 11;

SoftwareSerial mySerial(rxPin, txPin);
ModbusRTUSlave modbus(mySerial, driverEnablePin);  // defining modbus rtu slave
const uint8_t inputs[3] = { 2, 3, 4 };             // defining inputs pins

/* defining ModBus inputs, registers.. */
bool discreteInputs[3];
bool coils[2];
uint16_t commands[3];

uint8_t slaveID;  // slave ID for ModBus, the default is 1

void (*reboot)(void) = 0;  // reset function, at address 0, calling it will make the board reboot

void firstBoot() {  // first boot function, to reset memory location in which is stored the slave ID
  if (EEPROM[64] != 64) {
    EEPROM.update(64, 64);
    EEPROM.update(1, 1);
  } else {
    EEPROM.update(63, 1);
  }
}

void setup() {
  Serial.begin(9600);  // DEBUG
  firstBoot();

  slaveID = EEPROM.read(1);

  pinMode(inputs[0], INPUT_PULLUP);
  pinMode(inputs[1], INPUT_PULLUP);
  pinMode(inputs[2], INPUT_PULLUP);

  modbus.configureDiscreteInputs(discreteInputs, 3);  // bool array of discrete inputs values, number of discrete inputs
  modbus.configureCoils(coils, 2);                    // the two coils, if they're both up we can change the holdingregister value
  modbus.configureHoldingRegisters(commands, 3);      // the value used to update the slave ID
  modbus.begin(slaveID, 9600);
}

void loop() {
  discreteInputs[0] = !digitalRead(inputs[0]);  // putting inputs pin data into ModBus variables, digital values are inverted
  discreteInputs[1] = !digitalRead(inputs[1]);
  discreteInputs[2] = !digitalRead(inputs[2]);

  modbus.poll();

  if (commands[0] == 0x00C0 && commands[1] == 0x0050) {  // update slave ID
    EEPROM.write(1, commands[2]);
    reboot();
  }
}