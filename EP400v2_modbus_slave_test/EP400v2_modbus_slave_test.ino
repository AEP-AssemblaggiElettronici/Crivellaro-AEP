#include <ModbusRTUSlave.h>
#include <SoftwareSerial.h>

const byte driverEnablePin = 2;
const byte rxPin = 10;
const byte txPin = 11;

SoftwareSerial mySerial(rxPin, txPin); // use SofrwareSerial library, put "Serial" on ModbusRTUSlave if you wanna use board's default
ModbusRTUSlave modbus(mySerial, driverEnablePin); // defining modbus rtu slave
const byte inputs[3] = {2, 3, 4}; // defining inputs pins
bool discreteInputs[3]; // defining modbus things, registers etc.

void setup() {
  pinMode(inputs[0], INPUT_PULLUP);
  pinMode(inputs[1], INPUT_PULLUP);
  pinMode(inputs[2], INPUT_PULLUP);

  modbus.configureDiscreteInputs(discreteInputs, 3);     // bool array of discrete inputs values, number of discrete inputs
  modbus.begin(1, 9600);
}

void loop() {
  discreteInputs[0] = !digitalRead(inputs[0]); // putting inputs pin data into ModBus variables, digital values are inverted
  discreteInputs[1] = !digitalRead(inputs[1]);
  discreteInputs[2] = !digitalRead(inputs[2]);

  modbus.poll();
}
