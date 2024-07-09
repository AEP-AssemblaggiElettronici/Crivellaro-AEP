// --------------------------------------------------
//
// Code for control of ESP32 through MIT inventor app (Bluetooth). 
// device used for tests: ESP32-WROOM-32D
//
// 
//
// --------------------------------------------------

// this header is needed for Bluetooth Serial -> works ONLY on ESP32
#include "BluetoothSerial.h" 

#include "analogWrite.h"

#define LED 13

#define AN_OUT 33
#define POT_IN 35
#define DIG_OUT 12      // Riporta accensione o spegnimento pompa

#define ACCENDI 25
#define SPEGNI 26


#define PWM_PIN 5


// init Class:
BluetoothSerial ESP_BT; 


// Parameters for Bluetooth interface
int incoming;
int cicli=0;
bool accensione=0;
boolean primo=1;
int intensita_pompa=0;
int intensita_old=0;
int potenziometro=0;
int potenziometro_old=0;

boolean controllo_remoto=0;



void setup() {
  Serial.begin(19200);
  ESP_BT.begin("Inverter Pozzo"); //Name of your Bluetooth interface -> will show up on your phone

  pinMode (18, OUTPUT);
  pinMode (17, INPUT);
  pinMode (16, INPUT);
  pinMode (ACCENDI, OUTPUT);
  pinMode (SPEGNI, OUTPUT);
  pinMode (27, OUTPUT);
  pinMode (14, OUTPUT);
  //pinMode (PWM_PIN, OUTPUT);
  pinMode (LED, OUTPUT);

  pinMode (DIG_OUT, INPUT);
  pinMode (AN_OUT, INPUT);
  pinMode (POT_IN, INPUT);
  
  digitalWrite(18, LOW);

  digitalWrite(LED, LOW);
  digitalWrite(ACCENDI, LOW);
  digitalWrite(SPEGNI, LOW);
  digitalWrite(27, LOW);
  digitalWrite(14, LOW);

  
  //analogWriteResolution(5, 8);

  delay(1000);
  digitalWrite(SPEGNI, HIGH);
  delay(500);
  digitalWrite(SPEGNI, LOW);


          potenziometro=0;
        
        for(int x = 0; x < 20; x++) {
             potenziometro += analogRead(POT_IN);     //lettura 12 bit
             delay(5);
        }
        potenziometro=potenziometro/20;
        potenziometro=potenziometro>>4 ;               //conversione 8 bit
        analogWrite(PWM_PIN, potenziometro);

}

void loop() {

  if(primo==1)
  {primo=0;
  delay(1000);
  digitalWrite(SPEGNI, HIGH);
    delay(500);
  digitalWrite(SPEGNI, LOW);
    delay(1000);
  digitalWrite(SPEGNI, HIGH);
    delay(500);
  digitalWrite(SPEGNI, LOW);
    delay(1000);
  digitalWrite(SPEGNI, HIGH);
    delay(500);
  digitalWrite(SPEGNI, LOW);
    delay(1000);
  digitalWrite(SPEGNI, HIGH);
    delay(500);
  digitalWrite(SPEGNI, LOW);
    delay(1000);
  digitalWrite(SPEGNI, HIGH);
    delay(500);
  digitalWrite(SPEGNI, LOW);
    delay(1000);
  digitalWrite(SPEGNI, HIGH);
    delay(500);
  digitalWrite(SPEGNI, LOW);
  }
  
  
  // -------------------- Receive Bluetooth signal ----------------------
  if (ESP_BT.available()) 
  {
    incoming = ESP_BT.read(); //Read what we receive 

    Serial.print("Incoming:"); Serial.println(incoming,DEC);


    //  if(incoming==1 && accensione==0)
      if(incoming==1)
       {digitalWrite(ACCENDI, HIGH);
       delay(200);
       digitalWrite(ACCENDI, LOW);
       //accensione=1;
       //digitalWrite(LED, HIGH);
       }
   //   else if(incoming==2 && accensione==1)
      else if(incoming==2)
       {digitalWrite(SPEGNI, HIGH);
       delay(200);
       digitalWrite(SPEGNI, LOW);
       //accensione=0;
       //digitalWrite(LED, LOW);
       }    
        
       if(incoming>=10)
       intensita_pompa=(incoming/2)+125;
       
       Serial.print("Pompa:"); Serial.println(intensita_pompa,DEC);
       Serial.print("ACCESO:"); Serial.println(accensione,DEC);
       Serial.print("Pot:"); Serial.println(potenziometro,DEC);

       
       delay(5);

  }

     
      if(intensita_pompa>intensita_old+2 || intensita_pompa<intensita_old-2)
       {intensita_old=intensita_pompa;
        controllo_remoto=1;
        analogWrite(PWM_PIN, intensita_pompa);}
       else if( potenziometro > potenziometro_old+6 || potenziometro < potenziometro_old-6)
       {potenziometro_old = potenziometro;
        controllo_remoto=0;
        Serial.print("Pot:"); Serial.println(potenziometro,DEC);
        Serial.print("Analog Out:"); Serial.println(analogRead(AN_OUT),DEC);
        analogWrite(PWM_PIN, potenziometro);}
   
       
       if(accensione==1 && cicli>50 && controllo_remoto==0)
       {digitalWrite(LED, !digitalRead(LED));
       cicli=0;}
       else if(accensione==1 && cicli>15 && controllo_remoto==1)
       {digitalWrite(LED, !digitalRead(LED));
       cicli=0;}
       else if(accensione==0)
       digitalWrite(LED, LOW);


       // accensione= digitalRead(DIG_OUT);

       if(analogRead(AN_OUT)>100)
        accensione=1;
        else
        accensione=0;

        
        potenziometro=0;
        
        for(int x = 0; x < 10; x++) {
             potenziometro += analogRead(POT_IN);     //lettura 12 bit
             delay(1);
        }
        potenziometro=potenziometro/10;
        potenziometro= potenziometro>>4 ;               //conversione 8 bit
      //  if(potenziometro<70)
      //  potenziometro=70;
        potenziometro=(potenziometro/2)+125;

      
     

        ++cicli;
        delay(1);

  
}
