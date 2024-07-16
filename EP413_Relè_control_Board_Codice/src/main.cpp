
#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif
#define DELAY 20

#include "Wire.h"
#include "clsPCA9555.h"

PCA9555 ioport(0x20);

uint8_t detect[6] = { 0, 0, 0, 0, 0, 0 }; // stati dei vari relè: (0: spento, 1: on, 2: attesa dopo on: 3: off)

void setup()
{
  ioport.begin();
  ioport.setClock(400000);

  for (uint8_t i = 0; i < 6; i++) ioport.pinMode(i, OUTPUT); // definizione degli output
  for (uint8_t i = 8; i <= 15; i++)  ioport.pinMode(i, INPUT); // definizione degli input dei switch
  for (uint8_t i = 2; i <= 7; i++) pinMode(i, INPUT_PULLUP); // definizione degli input relay
  for (uint8_t i = 0; i < 6; i++) ioport.digitalWrite(i, LOW); // si assicura che lo stato dei relay sia basso all'avvio
}

void loop()
{
  bool interlock = !ioport.digitalRead(14) && !ioport.digitalRead(8) && !ioport.digitalRead(9);

  for (int i = 2; i <= 7; i++)
  {
    // MODE 1, finchè c'è corrente tiene il relay acceso
    if (ioport.digitalRead(i + 6))
    {
      if (!digitalRead (i))
      ioport.digitalWrite((i - 2), HIGH);
      else
      ioport.digitalWrite((i - 2), LOW);
    }
  
    // MODE 2 // quando c'è corrente cambia lo stato del relay ON / OFF
    if (!ioport.digitalRead(i + 6))
    {
      if (!digitalRead(i) && detect[i - 2] == 0)
      {
        if (!interlock || (interlock && i != 2 && i != 3)) // se interlock non è attivato il toggle è valido per ogni relay
        {
          delay(DELAY);
          if (!digitalRead(i))
          {
            detect[i - 2] = 1;
            ioport.digitalWrite((i - 2), HIGH);
          }
        }
        else if (i == 2 && !ioport.digitalRead(1)) // se interlock è attivato, la logica dei primi 2 relay cambia
        {
          delay(DELAY);
          if (!digitalRead(2))
          {
            detect[2 - 2] = 1;
            ioport.digitalWrite(0, HIGH);
          }
        }
        else if (i == 3 && !ioport.digitalRead(0))
        {
          delay(DELAY);
          if (!digitalRead(3))
          {
            detect[3 - 2] = 1;
            ioport.digitalWrite(1, HIGH);
          }
        }
      }
      if (digitalRead(i) && detect[i - 2] == 1) // cambio stato a corrente mancante sul relay (va a stato 2 dopo il toggle on)
      {
        delay(DELAY);
        if (digitalRead(i)) detect[i - 2] = 2;
      }

      if (!digitalRead(i) && detect[i - 2] == 2) 
      {
        if (!interlock || (interlock && i != 2 && i != 3)) // se interlock non è attivato il toggle è valido per ogni relay
        {
          delay(DELAY);
          if (!digitalRead(i))
          {
            detect[i - 2] = 3;
            ioport.digitalWrite((i - 2), LOW);
          }
        }
        else if (i == 2 && !ioport.digitalRead(1)) // se interlock è attivato, la logica dei primi 2 relay cambia
        {
          delay(DELAY);
          if (!digitalRead(2))
          {
            detect[2 - 2] = 3;
            ioport.digitalWrite(0, LOW);
          }
        }
        else if (i == 3 && !ioport.digitalRead(0))
        {
          delay(DELAY);
          if (!digitalRead(3))
          {
            detect[3 - 2] = 3;
            ioport.digitalWrite(1, LOW);
          }
        }
      }
      if (digitalRead(i) && detect[i - 2] == 3) // cambio stato a corrente mancante sul relay (ritorna allo stato 0 dopo il toggle off)
      {
        delay(DELAY);
        if (digitalRead(i)) detect[i - 2] = 0;
      }
    }
  }
}
