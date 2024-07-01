#include <Arduino.h>
#include <WiFi.h>
#include <RV-3028-C7.h>
#include <SPI.h>
#include <ESPFlash.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

RV3028 cl0ck;

void setup() {
    Serial.begin(115200);
    /* Wire.begin();
    while (cl0ck.begin() == false) {
        Serial.println("Something went wrong, check wiring");
        delay(1000);
    }
    Serial.println("cl0ck online!");
    delay(1000); */

    // Test WiFi
/*     WiFi.mode(WIFI_AP_STA); // wifi può connettersi ad un accesspoint e anche essere usato come accesspoint
    WiFi.softAP("Test_WiFi", NULL); // setta il nome ed un'eventuale chiave d'accesso all'accesspoint che creiamo
    WiFi.begin("AEP-Guest", "emilianoaep"); */

    // Test memoria flash
    delay(2000);
/*     SPIFFS.begin();
    Serial.print("Capacità memoria flash: ");
    Serial.print(SPIFFS.totalBytes());
    Serial.println(" bytes\n");
    delay(500);
    ESPFlash<String> file1("/file1");
    file1.append("TEST FILE 1");

    Serial.print(file1.get()); */

    // Test BLE
/*     delay(5000);
    Serial.println("Starting BLE work!");

    BLEDevice::init("MyESP32_2"); // nome del server bluetooth
    BLEServer *pServer = BLEDevice::createServer(); // creazione server bluetooth
    BLEService *pService = pServer->createService(SERVICE_UUID); // creazione di un servizio su server
    BLECharacteristic *pCharacteristic = pService->createCharacteristic( // creazione delle caratteristiche del servizio
                                            CHARACTERISTIC_UUID,
                                            BLECharacteristic::PROPERTY_READ |
                                            BLECharacteristic::PROPERTY_WRITE
                                        );

    pCharacteristic->setValue("Hello World says Neil"); // setta un valore del servizio
    pService->start();
    // BLEAdvertising *pAdvertising = pServer->getAdvertising();  // this still is working for backward compatibility
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising(); // crea la proprietà che permette ai dispositivi di trovare il server bluetooth
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising(); // avvia la scoperta del dispositivo da parte di hardware terzo
    Serial.println("Characteristic defined! Now you can read it in your phone!"); */
} 

void loop() {
    // Test WiFi
/*     delay(2000);
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print("Connesso al WiFi, inidirizzo IP:");
        Serial.print(WiFi.localIP());
        Serial.print("\n");
        Serial.print("Access point configurato come: ");
        Serial.print(WiFi.softAPSSID());
        Serial.print("\n");
    } */

    // I2C scan
/*     byte error, address;
    int nDevices;
    Serial.println("Scanning...");
    nDevices = 0;
    for(address = 1; address < 127; address++ ) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0) {
                Serial.print("I2C device found at address 0x");
                if (address<16) {
                Serial.print("0");
            }
            Serial.println(address,HEX);
            nDevices++;
        }
        else if (error==4) {
                Serial.print("Unknow error at address 0x");
                if (address<16) {
                Serial.print("0");
            }
            Serial.println(address,HEX);
        }    
    }
    if (nDevices == 0) {
        Serial.println("No I2C devices found\n");
    }
    else {
        Serial.println("done\n");
    }
    delay(5000);   */
}