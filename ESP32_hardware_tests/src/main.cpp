#include <Arduino.h>
#include <ESP32Time.h>
#include <WiFi.h>
#include <RV-3028-C7.h>

//ESP32Time realTimeClock(0);
//RV3028 realTimeClock;
bool wifiCheck = 1;
bool accessPointCheck = 1;

void setup() {
    //setTime(30, 24, 15, 17, 1, 2021);  // 17th Jan 2021 15:24:30
    //realTimeClock.setTime();
    WiFi.mode(WIFI_AP_STA); // wifi può connettersi ad un accesspoint e anche essere usato come accesspoint
    WiFi.softAP("Test_WiFi", NULL); // setta il nome ed un'eventuale chiave d'accesso all'accesspoint che creiamo
    WiFi.begin("AEP-Guest", "emilianoaep");

    Serial.begin(9600);
}

void loop() {
    delay(2000);
    if (WiFi.status() == WL_CONNECTED && wifiCheck)
    {
        Serial.print("Connesso al WiFi, inidirizzo IP:");
        Serial.print(WiFi.localIP());
        Serial.print("\n");
        wifiCheck = 0;
    }
    delay(2000);
    if (accessPointCheck)
    {
        Serial.print("Access point configurato come: ");
        Serial.print(WiFi.softAPSSID());
        accessPointCheck = 0;
    }
}