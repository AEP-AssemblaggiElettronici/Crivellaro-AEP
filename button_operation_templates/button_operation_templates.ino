/* tested on EP400v2 board, but good for arduino and co. */

#include <Timer.h>

#define BUTTON1 5          // EP400v2's button 1 pin
#define BUTTON2 4          // EP400v2's button 2 pin
#define PERIPHEAL_POWER 8  // EP400v2's peripheal power, same of " digitalWrite(pin, 1) "

bool button1otherMode = false;  // e control variable for button 1 other mode
bool button1lastStatus;         // control variable for button 1 release

Timer timer;

void setup() {
  pinMode(BUTTON1, INPUT);
  pinMode(BUTTON2, INPUT);

  timer.start();
  digitalWrite(PERIPHEAL_POWER, 1);  // peripheal power
  Serial.begin(9600);
}

void loop() {
  if (!digitalRead(BUTTON1)) {
    button1lastStatus = true;
    timer.resume();
  } else {
    if (button1lastStatus && !button1otherMode) {
      Serial.println("Button 1 released");
    }
    timer.pause();
    button1lastStatus = false;
  }

  if (timer.read() % 5000 == 0 && timer.read() >= 5000) {  // long press ModBus button for 5 seconds to enter edit mode
    if (!button1otherMode && !digitalRead(BUTTON1)) {
      Serial.println("Button 1 second function enabled");
      button1otherMode = true;
    }
    delay(100);
  }

  if (!digitalRead(BUTTON2) && button1otherMode) {
    Serial.println("Revert to Button 1 first function");
  } else 
}
