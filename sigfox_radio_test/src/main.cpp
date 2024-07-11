#include <Arduino.h>
#include <SoftwareSerial.h>

#define RX 6
#define TX 7
#define SIGFOX_RESET_PIN 2

SoftwareSerial radio(RX, TX);
String messageHead = "AT$SF=";
String getID = "AT$I=10\r";
String getTXfreq = "AT$IF?\r";
String getRXfreq = "AT$DR?\r";
String dummy = "AT\r";
String info = "AT$I=0\r";
String resp = "";
int contatore = 0;

void setup() 
{
  Serial.begin(9600);
  pinMode(SIGFOX_RESET_PIN, OUTPUT);
}

void loop() 
{
  //test raccolta info dal dispositivo (id ecc.)
  while (contatore < 1) 
  {
    digitalWrite(SIGFOX_RESET_PIN, 1);
    radio.begin(9600);
    delay(200);
    radio.print(getID);
    delay(1000);
    while(radio.available()) 
    {
      resp += (char)radio.read();
    }
    Serial.println(resp);
    digitalWrite(SIGFOX_RESET_PIN, 0);
    radio.end();
    contatore++;
  }

  // test trasmissione
/*   if (millis() % 10000 == 0) {
    digitalWrite(SIGFOX_RESET_PIN, 1);
    radio.begin(19200);
    delay(1000);
    radio.print(messageHead + "Lorem ipsum dolor sit amet, consectetur adipiscing elit. In sed scelerisque erat. Donec non tellus sit amet neque elementum egestas. Class aptent taciti sociosqu ad litora torquent per conubia nostra, per inceptos himenaeos. Sed sit amet orci bibendum tellus convallis dictum non non ligula. Ut fringilla augue arcu, nec venenatis mi dictum et. Mauris venenatis velit libero, nec varius orci elementum ac. Orci varius natoque penatibus et magnis dis parturient montes, nascetur ridiculus mus. Donec malesuada mi dignissim arcu elementum, vel tincidunt urna vestibulum. Morbi eu dui id turpis tempus feugiat. Integer fringilla molestie elit, eget finibus mauris semper consectetur. Integer elit quam, hendrerit non mi convallis, gravida dictum eros. Pellentesque malesuada quam volutpat, egestas dui quis, facilisis erat. Praesent tortor sem, tristique vitae mauris sit amet, cursus faucibus lectus. Nunc viverra dolor et dolor mattis vehicula.\r");
    contatore++;
    delay(1000);
    digitalWrite(SIGFOX_RESET_PIN, 0);
    radio.end();
    Serial.println("Messaggio inviato " + (String)contatore);
  } */
}
