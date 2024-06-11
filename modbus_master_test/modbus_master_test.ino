#include <SoftwareSerial.h>
#include <ModbusRTUMaster.h>

const byte rxPin = 10;
const byte txPin = 11;
const byte driverEnablePin = 13;

SoftwareSerial mySerial(rxPin, txPin);
ModbusRTUMaster modbus(mySerial, driverEnablePin);

bool discreteInputs[3];

void setup() {
  modbus.begin(9600);
  Serial.begin(9600);
}

void loop() {
  /* parameters on the method:
   slave id, starting data address, bool array to place discrete input values, number of discrete inputs to read: */
  modbus.readDiscreteInputs(1, 0, discreteInputs, 3);

  Serial.println("Inputs array:"); //test
  Serial.println(discreteInputs[0]);
  Serial.println(discreteInputs[1]);
  Serial.println(discreteInputs[2]);
  delay(5000);
}
