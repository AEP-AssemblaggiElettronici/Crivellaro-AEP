#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <SPIFFS.h>
#include <Update.h>
#include <ArduinoJson.h>

#define FILENAME "centralina_irrigazione"
#define EEPROM_VERSION_INT_LOCATION 0
#define EEPROM_VERSION_DEC_LOCATION 1

String filename;
unsigned short int ver_int;
unsigned short int ver_dec;

WiFiServer serv(8088);
WiFiClient client(8088);

void update_version(unsigned short int, unsigned short int);
void get_version(WiFiClient);
void get_current_version();

void setup()
{
  Serial.begin(9600);

  // EEPROM operations
  // init:

  /*   IPAddress IPlocale(192, 168, 8, 8);
    IPAddress gateway(192, 168, 8, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.config(IPlocale, gateway, subnet); */
  WiFi.begin("HOTSPOT_TEST", "hotspot_test");
  serv.begin();
  client.connect("localhost", 8080);
}

void loop()
{
  /*   if (WiFi.isConnected())
    {
      delay(5000);
      Serial.println(WiFi.localIP());
    } */

  WiFiClient clientRemoto = serv.available();
  get_version(clientRemoto);
}

void update_version(unsigned short int i, unsigned short int d) // update versione
{
  Serial.println("Current version is " + (String)ver_int + "." + (String)ver_dec);
  // aggiorna le variabili di versione
  ver_int = i;
  ver_dec = d;
  /*   SPIFFS.begin();
    SPIFFS.format();
    File version = SPIFFS.open("/version.txt", FILE_WRITE);
    version.printf("%s.%s", i, d);
    version.close();
    SPIFFS.end(); */

  Serial.println("Updating to " + (String)ver_int + "." + (String)ver_dec + " ...");
  //
  if (client.connected())
    client.println("UPDATE_REQUEST");
  else
    Serial.println("NC");
}

void get_version(WiFiClient clientRemoto)
{
  if (clientRemoto)
  {
    // Serial.println("Client connesso.");
    while (clientRemoto.connected())
    {
      if (clientRemoto.available())
      {
        JsonDocument jay;
        String data = clientRemoto.readString(); // prende l'input  dal client
        deserializeJson(jay, data);              // trasforma in json
        String filename = jay["filename"];
        String versionStr = jay["version"];
        // converte in int interi e decimali del numero versione in formato stringa:
        unsigned short int version[2] = {
            (versionStr.substring(0, versionStr.indexOf('.'))).toInt(),
            (versionStr.substring(versionStr.indexOf('.') + 1, versionStr.length())).toInt()};

        // Serial.println(filename + " " + versionStr);

        // check versione
        get_current_version();
        if (filename == FILENAME)
        {
          if (version[0] > ver_int)
            update_version(version[0], version[1]);
          else if (version[0] == ver_int)
          {
            if (version[1] > ver_dec)
              update_version(version[0], version[1]);
          }
        }
        else
          Serial.println("Nome di file non valido!");
        //
      }
    }
  }
}

void get_current_version()
{
  SPIFFS.begin();
  File version = SPIFFS.open("/version.txt", FILE_READ);
  ver_int = (version.readString().substring(0, version.readString().indexOf("."))).toInt();
  ver_dec = (version.readString().substring(version.readString().indexOf(".") + 1, version.readString().length())).toInt();
  version.close();
  SPIFFS.end();
}