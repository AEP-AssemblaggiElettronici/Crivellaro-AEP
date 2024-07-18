#include <Arduino.h>
#include <WiFi.h>
#include <EEPROM.h>
#include <ESPAsyncWebServer.h>
#include <WString.h>
#include "secret.h"

#define DELAY_LOCATION 0
#define RELAY 14
#define IN1 23
#define LEDOUT 13
#define HOSTNAME "delay"

bool trigger = 0;                      // variabile di controllo impulso ricevuto
unsigned short int tempoDelay;         // variabile del delay
const char *nomeHost = "RitardoForno"; // stringa con nome host
const char *RITARDO = "ritardoPagina"; // costante che prende il parametro in <input name="XXX"> nella form html, in questo caso "ritardoPagina"

// crea la pagina web:
const char page[] PROGMEM = R"___(
<html>
  <!-- foglio di stile della pagina HTML dello webserver -->
  <style>
    #box{
        display: flex;
        flex-diretcion: row;
        alilgn-items: center;
        justify-content: center;
        align-content: center;
        align-self: center;
        padding: 10%;
    }
    #card{
        display: flex;
        justify-content: center;
        align-content: center;
        align-self: center;
        max-width: 600px;
        min-height: 250px;
        background: #333333;
        padding: 30px;
        color: #FFF;
        font-size: 30px;
        margin:20px;
        box-shadow: 0px 8px 22px -8px rgba(0,0,0,0.50);
        border-radius: 10%;
    }
    input{
      font-size: 130%;
    }
  </style>

  <!-- ritorniamo il valore del ritardo tramite questa promise JS che lo scrive nello span dell'HTML,
       ogni secondo il valore viene refreshato in modo che se viene cambiato da diversi dispositivi
       rimane sempre aggiornato -->
  <script>
    setInterval(()=>{
      fetch("/prendiTempoDelay").then(r => {
        return r.json();
      }).then(rr => {
        document.getElementById("tempoDelay").innerHTML = rr;
      });
    }, 1000);
  </script>

  <!-- codice HTML del webserver -->
  <body>
    <div id="box">
      <div id="card">
        <form align="center" action="/invia">
          <p>Inserire tempo di delay 
          <br>(in secondi, valore massimo 60)</p> 
          <p>Lasciare vuoto per impostare a 0</p>
          <p>Attuale ritardo: <span id="tempoDelay"</span></p>
          <input name="ritardoPagina" type="number" min="0" max="60"/>
          <input type="submit" value="OK"/>
        </form>
      </div>
    </div>
  </body>
</html>
)___";
//

AsyncWebServer server(80); // server su port HTTP 80 standard

void setup()
{
  Serial.begin(9600);
  while (!Serial)
  {
  } // finchè non parte la seriale, non fare nulla = )

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
  tempoDelay = EEPROM.read(DELAY_LOCATION);
  //

  // settaggio hostname e IP statico
  IPAddress IPlocale(192, 168, 1, 222); // l'ultima cifra da cambiare a gusto proprio, la penultima a seconda del router
  IPAddress gateway(192, 168, 1, 1);    // da cambiare a seconda del router
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

  // indirizzo home del server web su scheda con ESP32 (espertino in questo caso)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send_P(200, "text/html", page); });
  //

  // prende il valore di tempoDelay da visualizzare sulla pagina
  server.on("/prendiTempoDelay", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plane", (String)tempoDelay); });
  //

  // quando si preme l'ok nella form avviene questa callback
  server.on("/invia", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              if (request->hasParam(RITARDO)) tempoDelay = abs(request->getParam(RITARDO)->value().toInt()); // prende il valore (se lo scrivi negativo gli fa il valore assoluto)
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
  if (!digitalRead(IN1) && !trigger)
  {
    trigger = 1;
    delay(tempoDelay * 1000);
    digitalWrite(RELAY, 1);
    digitalWrite(LEDOUT, 0);
    delay(300);
    digitalWrite(RELAY, 0);
    digitalWrite(LEDOUT, 1);
    trigger = 0;
  }
}