#include <Arduino.h>
#include "wiring_private.h"

#define RX 11
#define TX 10
#define SIGFOX_RESET_PIN 16

// IMPORTANTE! Far seguire '\r' (equivale a premere invio) a fine comando, altrimenti non lo invia al dispositivo
String messageHead = "AT$SF=";  // per inviare un messaggio sigfox
String getID = "AT$I=10\r";
String getPAC = "AT$I=11\r";
String getTXfreq = "AT$IF?\r";
String getRXfreq = "AT$DR?\r";
String dummy = "AT\r";
String info = "AT$I=0\r";

String command(String command);

Uart Serial2(&sercom1, RX, TX, SERCOM_RX_PAD_0, UART_TX_PAD_2);

void SERCOM1_Handler() {
  Serial2.IrqHandler();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Test");
  Serial.end();

  Serial2.begin(9600);

  // Assign pins 10 & 11 SERCOM functionality
  pinPeripheral(10, PIO_SERCOM);
  pinPeripheral(11, PIO_SERCOM);

  pinMode(SIGFOX_RESET_PIN, OUTPUT);

  digitalWrite(SIGFOX_RESET_PIN, HIGH);
  delay(200);
  digitalWrite(SIGFOX_RESET_PIN, LOW);
  delay(200);
  digitalWrite(SIGFOX_RESET_PIN, HIGH);

  delay(100);
  //getID();
  delay(100);
  Serial2.print("AT$I=10\r");
  delay(500);

  Serial2.print("AT$I=11\r");
  delay(500);

  //  Serial2.end();
}

void loop() {
  // test raccolta info dal dispositivo (id ecc.)
  digitalWrite(SIGFOX_RESET_PIN, 1);
  delay(200);
  Serial.println();
  Serial.print("ID: ");

  Serial.print(command(getID));
  Serial.println();
  delay(250);
  Serial.print("PAC: ");
  Serial.print(command(getPAC));
  digitalWrite(SIGFOX_RESET_PIN, 0);
  delay(1000);
}

String command(String command) {
  delay(50);
  String resp = "";
  Serial2.begin(9600);
  delay(200);
  Serial2.print(command);
  delay(1000);
  while (Serial2.available())
    resp += (char)Serial2.read();
  Serial2.end();
  return resp;
}
