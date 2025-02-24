#include <Arduino.h>
#include <SPI.h>
#include <WiFi101.h>

uint8_t noWiFi = 0;
int hotspots;

void setup()
{
  WiFi.setPins(8, 7, 4, 2); // questo è essenziale per far funzionare il wifi sull'adafruit feather
  Serial.begin(9600);
  while (!Serial)
    ;
  if (WiFi.status() == 255)
  {
    Serial.println("No WiFi shield detected");
    noWiFi = 1;
  }
}

void loop()
{
  if (!noWiFi)
  {
    hotspots = WiFi.scanNetworks();

    if (hotspots != -1)
    {
      for (int i = 0; i < hotspots; i++)
        Serial.println(WiFi.SSID(i));
    }
    Serial.println("___");
    delay(2000);
  }
}
