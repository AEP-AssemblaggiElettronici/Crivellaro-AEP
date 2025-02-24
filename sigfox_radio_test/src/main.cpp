#include <Arduino.h>
#include <SoftwareSerial.h>

#define RX 6
#define TX 7
#define SIGFOX_RESET_PIN 2

// IMPORTANTE! Far seguire '\r' (equivale a premere invio) a fine comando, altrimenti non lo invia al dispositivo
SoftwareSerial radio(RX, TX);
String messageHead = "AT$SF=";
String getID = "AT$I=10\r";
String getPAC = "AT$I=11\r";
String getTXfreq = "AT$IF?\r";
String getRXfreq = "AT$DR?\r";
String dummy = "AT\r";
String info = "AT$I=0\r";

String command(String command);

void setup()
{
  Serial.begin(9600);
  pinMode(SIGFOX_RESET_PIN, OUTPUT);
  while (!Serial)
    ;
  // test raccolta info dal dispositivo (id ecc.)
  Serial.print("ID: ");
  Serial.print(command(getID));
  Serial.println();
  Serial.print("PAC: ");
  Serial.print(command(getPAC));
}

void loop() {}

String command(String command)
{
  String resp = "";
  digitalWrite(SIGFOX_RESET_PIN, 1);
  radio.begin(9600);
  delay(200);
  radio.print(command);
  delay(1000);
  while (radio.available())
    resp += (char)radio.read();
  digitalWrite(SIGFOX_RESET_PIN, 0);
  radio.end();
  return resp;
}
