#include "HX711.h"

#define BOOST_SHTDWN 19
#define BOOST_EN 20
#define PIN_FORK_C 4  // prima era 1, che dava errore, ok input ma non output, sotto
#define PIN_BIL_C 3   // ok, sopra
#define PIN_FORK_D 1  // prima era 6, che dava errore, sotto
#define PIN_BIL_D 36  // ok, sopra

// HX711 circuit wiring
const int LOADCELL_DOUT_PIN = /* PIN_FORK_C */ /* PIN_BIL_C */ PIN_BIL_D /* PIN_FORK_D */;
const int LOADCELL_SCK_PIN = /* PIN_BIL_C */ /* PIN_FORK_C */ PIN_FORK_D /* PIN_BIL_D */;

HX711 scale;

void setup() {
  pinMode(BOOST_EN, OUTPUT);
  pinMode(BOOST_SHTDWN, OUTPUT);
  digitalWrite(BOOST_EN, 1);
  digitalWrite(BOOST_SHTDWN, 0);
  delay(100);
  Serial.begin(115200);
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
}

void loop() {

  if (scale.is_ready()) {
    long reading = scale.read();
    Serial.print("HX711 reading: ");
    Serial.println(reading);
  } else {
    Serial.println("HX711 not found.");
  }

  delay(1000);
}
