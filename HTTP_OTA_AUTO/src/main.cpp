#include <Arduino.h>
#include <WiFi.h>
#include "http_ota_update.h"

#define SERVER "http://192.168.8.201:1880/ver"

void setup()
{
  Serial.begin(9600);
  WiFi.begin("HOTSPOT_TEST", "hotspot_test");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(250);
  }
  Serial.print("WiFi connessa: ");
  Serial.print(WiFi.localIP());
  Serial.println();
}

void loop()
{
  if (Serial.read() == 'd')
    http_ota_update(SERVER);
}