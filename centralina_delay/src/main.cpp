#include <Arduino.h>
#include <WiFi.h>
#include <EEPROM.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <ElegantOTA.h>
#include <FS.h>
#include "secret.h"
#include "page.h"

#define DELAY_LOCATION 0
#define RELAY 14
#define IN1 23
#define LEDOUT 13
#define HOSTNAME "delay"

bool ingresso;
unsigned short int stato = 0;
unsigned long tempo = 0;
/* bool variabileControllo = 1; */
unsigned short int tempoDelay;         // variabile del delay
const char *nomeHost = "RitardoForno"; // stringa con nome host
const char *RITARDO = "ritardoPagina"; // costante che prende il parametro in <input name="XXX"> nella form html, in questo caso "ritardoPagina"

// crea la pagina web:
//const char page[] PROGMEM = R"___()___";
//

AsyncWebServer server(80); // server su port HTTP 80 standard

void setup()
{
  Serial.begin(9600);
  while (!Serial)
  {
  } // finchè non parte la seriale, non fare nulla = )

  pinMode(RELAY, OUTPUT);
  pinMode(IN1, INPUT_PULLDOWN);
  pinMode(LEDOUT, OUTPUT);

  // inizializzazione EEPROM
  EEPROM.begin(2);
  if (EEPROM.read(1) != 1)
  {
    EEPROM.write(1, 1);
    EEPROM.write(DELAY_LOCATION, 5);
    EEPROM.commit();
  }
  tempoDelay = EEPROM.read(DELAY_LOCATION);
  //

  // settaggio hostname e IP statico
  IPAddress IPlocale(192, 168, 8, 222); // l'ultima cifra da cambiare a gusto proprio, la penultima a seconda del router
  IPAddress gateway(192, 168, 8, 1);    // da cambiare a seconda del router
  IPAddress subnet(255, 255, 255, 0);   // da cambiare a seconda del router
  WiFi.config(IPlocale, gateway, subnet);
  WiFi.setHostname(nomeHost); // define hostname
  //

  // inizializzazione WiFi
  WiFi.begin(SSID, PWRD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connessa");
  Serial.println("Indirizzo IP: " + (String)WiFi.localIP());
  //

  ElegantOTA.begin(&server);

  // indirizzo home del server web su scheda con ESP32 (espertino in questo caso)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/html", page); });
  //

  // prende il valore di tempoDelay da visualizzare sulla pagina
  server.on("/prendiTempoDelay", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plane", (String)tempoDelay); });
  //

  // quando si preme l'ok nella form avviene questa callback
  server.on("/invia", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              if (request->hasParam(RITARDO))
                tempoDelay = abs(request->getParam(RITARDO)->value().toInt()); // prende il valore (se lo scrivi negativo gli fa il valore assoluto)
              EEPROM.write(DELAY_LOCATION, tempoDelay);
              EEPROM.commit();
              request->redirect("/"); // torna alla pagina di input
            });
  //

  server.begin();

  digitalWrite(LEDOUT, 1);
}

void loop()
{
  ElegantOTA.loop();

/*   if (!digitalRead(IN1) && variabileControllo)
  {
    delay(100);
    variabileControllo = 0;
    if (!digitalRead(IN1))
    {
      delay(tempoDelay * 1000);
      digitalWrite(RELAY, 1);
      digitalWrite(LEDOUT, 0);
      delay(300);
      digitalWrite(RELAY, 0);
      digitalWrite(LEDOUT, 1);
    }
    variabileControllo = 1;
  }
  if (digitalRead(IN1))
  {
  } */

    if (digitalRead(IN1) == 0)
      delay(100);
    if (digitalRead(IN1) == 0)
      ingresso = 0;

    if (digitalRead(IN1) == 1)
      delay(100);
    if (digitalRead(IN1) == 1)
      ingresso = 1;

    switch (stato)
    {
    case 0:
      if (ingresso == 0)
        ++stato;
      break;
    case 1:
      if (tempo >= tempoDelay)
      {
        ++stato;
        tempo = 0;
      }
      delay(1000); // era 10 milisecondi, proviamo con 1 secondo
      ++tempo;
      break;
    case 2:
      digitalWrite(LEDOUT, LOW);
      digitalWrite(RELAY, HIGH);
      delay(300); // 200ms
      digitalWrite(LEDOUT, HIGH);
      digitalWrite(RELAY, LOW);
      ++stato;
      break;
    case 3:
      if (ingresso == 1)
        stato = 0;
      break;
    }
}