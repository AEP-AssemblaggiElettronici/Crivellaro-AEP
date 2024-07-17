#include <Arduino.h>
#include <WiFi.h>
#include <EEPROM.h>
#include <ESPAsyncWebServer.h>
#include <WString.h>

#define SSID "HOTSPOT_TEST"
#define PWRD "hotspot_test"
#define DELAY_LOCATION 0
#define RELAY 14
#define IN1 23
#define LEDOUT 13
#define HOSTNAME "delay"

int tempoDelay = 5000; // variabile del delay (default 5000)
const char* nomeHost = "RitardoForno"; // stringa con nome host
const char* RITARDO = "ritardoPagina"; // costante che prende il parametro in <input name="XXX"> nella form html, in questo caso "ritardoPagina"
// crea la pagina web:
const char page[] PROGMEM = R"rawliteral(
<html>
  <body>
     <form align="center" action="/invia">
       <p>Inserire tempo di delay (in millisecondi, valore massimo 30000) lasciare vuoto per impostare a 0</p>
       <input name="ritardoPagina" type="number" min="0" max="30000"/>
       <input type="submit" value="OK"/>
     </form>
   </body>
</html>)rawliteral";
//

AsyncWebServer server(80); // server su port HTTP 80 standard

void setup()
{
  Serial.begin(9600);
  while (!Serial){} // finchè non parte la seriale, non fare nulla = )

  pinMode(RELAY, OUTPUT);
  pinMode(IN1, INPUT);
  pinMode(LEDOUT, OUTPUT);

// inizializzazione EEPROM
  EEPROM.begin(2);
  if (EEPROM.read(1) != 1)
  {
    EEPROM.write(1, 1);
    EEPROM.write(DELAY_LOCATION, 5);
    EEPROM.commit();
  }
//

// settaggio hostname
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(nomeHost.c_str()); //define hostname
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

// indirizzo home del server web su scheda con ESP32 (espertino in questo caso)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    request->send_P(200, "text/html", page);
  });
//

// quando si preme l'ok nella form avviene questa callback
  server.on("/invia", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (request->hasParam(RITARDO)) tempoDelay = abs(request->getParam(RITARDO)->value().toInt()); // prende il valore (se lo scrivi negativo gli fa il valore assoluto)
    request->send_P(200, "text/html", page); // torna alla pagina di input
  });
//

  server.begin();
}

void loop()
{
  if (!digitalRead(IN1))
  {
    delay(tempoDelay);
    digitalWrite(RELAY, 1);
  }
}