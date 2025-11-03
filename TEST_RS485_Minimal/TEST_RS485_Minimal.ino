/*
 * ====================================================================
 * TEST MINIMALE RS485 - CWT Soil Sensor
 * ====================================================================
 * 
 * VERSION: 1.1.0
 * DATE: 2025-10-24
 * 
 * Questo sketch serve SOLO per testare la comunicazione RS485
 * con il sensore CWT Soil, senza LoRa.
 * 
 * UTILIZZO:
 * 1. Caricare questo sketch su ESP32-S3
 * 2. Aprire Serial Monitor a 115200 baud
 * 3. Verificare che il sensore risponda correttamente
 * 4. Una volta verificato, caricare lo sketch completo v4.1.0
 * 
 * ====================================================================
 * CHANGELOG
 * ====================================================================
 * v1.1.0 - Output compatto in formato tabellare
 * v1.0.0 - Test iniziale RS485
 * ====================================================================
 */

#include <Arduino.h>
#include <HardwareSerial.h>

// ====================================================================
// CONFIGURAZIONE PIN
// ====================================================================
#define RS485_TX_PIN        15
#define RS485_RX_PIN        16
#define RS485_RE_PIN        17
#define RS485_DE_PIN        18
#define BOOST_SHUTDOWN_PIN  19
#define BOOST_ENABLE_PIN    20

// Parametri comunicazione
#define RS485_BAUD_RATE     4800
#define RS485_TIMEOUT_MS    1000
#define SOIL_SENSOR_ADDR    0x01

// Registri sensore
#define REG_HUMIDITY        0x0000
#define REG_TEMPERATURE     0x0001
#define REG_CONDUCTIVITY    0x0002
#define REG_PH              0x0003

// ====================================================================
// VARIABILI GLOBALI
// ====================================================================
HardwareSerial RS485Serial(2);

// ====================================================================
// FUNZIONI UTILITY
// ====================================================================

uint16_t calculateCRC16(uint8_t *buffer, uint8_t length) {
    uint16_t crc = 0xFFFF;
    for (uint8_t pos = 0; pos < length; pos++) {
        crc ^= (uint16_t)buffer[pos];
        for (uint8_t i = 8; i != 0; i--) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void rs485TransmitMode() {
    digitalWrite(RS485_RE_PIN, HIGH);
    digitalWrite(RS485_DE_PIN, HIGH);
    delayMicroseconds(10);
}

void rs485ReceiveMode() {
    digitalWrite(RS485_DE_PIN, LOW);
    digitalWrite(RS485_RE_PIN, LOW);
    delayMicroseconds(10);
}

void soilSensorPowerOn() {
    Serial.println("→ Power ON");
    digitalWrite(BOOST_SHUTDOWN_PIN, HIGH);
    digitalWrite(BOOST_ENABLE_PIN, HIGH);
    delay(200);
}

void soilSensorPowerOff() {
    Serial.println("→ Power OFF");
    digitalWrite(BOOST_SHUTDOWN_PIN, LOW);
    digitalWrite(BOOST_ENABLE_PIN, LOW);
}

void printHex(uint8_t value) {
    if (value < 16) Serial.print("0");
    Serial.print(value, HEX);
}

// ====================================================================
// LETTURA MODBUS
// ====================================================================

bool readModbusRegisters(uint16_t regAddress, uint8_t numRegisters, uint16_t *values) {
    // Costruisci richiesta
    uint8_t request[8];
    request[0] = SOIL_SENSOR_ADDR;
    request[1] = 0x03;  // Read Holding Registers
    request[2] = highByte(regAddress);
    request[3] = lowByte(regAddress);
    request[4] = 0x00;
    request[5] = numRegisters;
    
    uint16_t crc = calculateCRC16(request, 6);
    request[6] = lowByte(crc);
    request[7] = highByte(crc);
    
    // Mostra richiesta
    Serial.print("TX → ");
    for (uint8_t i = 0; i < 8; i++) {
        printHex(request[i]);
        Serial.print(" ");
    }
    Serial.println();
    
    // Svuota buffer RX
    while (RS485Serial.available()) {
        RS485Serial.read();
    }
    
    // Invia richiesta
    rs485TransmitMode();
    RS485Serial.write(request, 8);
    RS485Serial.flush();
    rs485ReceiveMode();
    
    // Attendi risposta
    uint32_t startTime = millis();
    uint8_t expectedBytes = 5 + (numRegisters * 2);
    uint8_t response[64];
    uint8_t bytesReceived = 0;
    
    while ((millis() - startTime) < RS485_TIMEOUT_MS && bytesReceived < expectedBytes) {
        if (RS485Serial.available()) {
            response[bytesReceived++] = RS485Serial.read();
        }
    }
    
    // Mostra risposta
    Serial.print("RX ← ");
    for (uint8_t i = 0; i < bytesReceived; i++) {
        printHex(response[i]);
        Serial.print(" ");
    }
    Serial.println();
    
    // Verifica risposta
    if (bytesReceived < expectedBytes) {
        Serial.print("✗ TIMEOUT - Attesi ");
        Serial.print(expectedBytes);
        Serial.print(" byte, ricevuti ");
        Serial.println(bytesReceived);
        return false;
    }
    
    if (response[0] != SOIL_SENSOR_ADDR || response[1] != 0x03) {
        Serial.println("✗ ERRORE - Header non valido");
        return false;
    }
    
    // Verifica CRC
    uint16_t receivedCRC = response[bytesReceived - 2] | (response[bytesReceived - 1] << 8);
    uint16_t calculatedCRC = calculateCRC16(response, bytesReceived - 2);
    
    if (receivedCRC != calculatedCRC) {
        Serial.print("✗ ERRORE CRC - Ricevuto: 0x");
        Serial.print(receivedCRC, HEX);
        Serial.print(", Calcolato: 0x");
        Serial.println(calculatedCRC, HEX);
        return false;
    }
    
    // Estrai dati
    for (uint8_t i = 0; i < numRegisters; i++) {
        values[i] = (response[3 + i * 2] << 8) | response[4 + i * 2];
    }
    
    Serial.println("✓ Lettura OK");
    return true;
}

// ====================================================================
// SETUP
// ====================================================================

void setup() {
    Serial.begin(115200);
    delay(500);
    
    Serial.println("\n╔════════════════════════════════════════════════════════════╗");
    Serial.println("║          TEST RS485 - CWT SOIL SENSOR v1.1.0             ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝");
    
    RS485Serial.begin(RS485_BAUD_RATE, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    pinMode(RS485_RE_PIN, OUTPUT);
    pinMode(RS485_DE_PIN, OUTPUT);
    rs485ReceiveMode();
    
    pinMode(BOOST_SHUTDOWN_PIN, OUTPUT);
    pinMode(BOOST_ENABLE_PIN, OUTPUT);
    soilSensorPowerOff();
    
    Serial.println("✓ Init OK | Starting tests in 2 seconds...\n");
    delay(2000);
}

// ====================================================================
// LOOP
// ====================================================================

void loop() {
    Serial.println("════════════════════════════════════════════════════════════");
    Serial.print("TEST #"); 
    Serial.print(millis() / 10000);
    Serial.println(" - Reading Soil Sensor");
    Serial.println("════════════════════════════════════════════════════════════");
    
    soilSensorPowerOn();
    delay(500);
    
    // ----------------------------------------------------------------
    // TEST 1: Lettura completa (7 registri)
    // ----------------------------------------------------------------
    Serial.println("\n▶ [1/3] ALL Parameters (7 regs)");
    uint16_t allValues[7];
    if (readModbusRegisters(REG_HUMIDITY, 7, allValues)) {
        Serial.println("┌─────────────┬───────────┐");
        Serial.println("│  Parameter  │   Value   │");
        Serial.println("├─────────────┼───────────┤");
        Serial.print("│ Humidity    │ "); Serial.print(allValues[0] / 10.0, 1); Serial.println(" %RH   │");
        Serial.print("│ Temperature │ "); Serial.print(allValues[1] / 10.0, 1); Serial.println(" °C    │");
        Serial.print("│ EC          │ "); Serial.print(allValues[2]); Serial.println(" us/cm │");
        Serial.print("│ PH          │ "); Serial.print(allValues[3] / 10.0, 1); Serial.println("       │");
        Serial.print("│ Nitrogen    │ "); Serial.print(allValues[4]); Serial.println(" mg/kg │");
        Serial.print("│ Phosphorus  │ "); Serial.print(allValues[5]); Serial.println(" mg/kg │");
        Serial.print("│ Potassium   │ "); Serial.print(allValues[6]); Serial.println(" mg/kg │");
        Serial.println("└─────────────┴───────────┘");
    } else {
        Serial.println("✗ FAILED\n");
    }
    
    delay(500);
    
    // ----------------------------------------------------------------
    // TEST 2: EC + PH (come nello sketch principale)
    // ----------------------------------------------------------------
    Serial.println("\n▶ [2/3] EC + PH (2 regs) - Main sketch mode");
    uint16_t ecphValues[2];
    if (readModbusRegisters(REG_CONDUCTIVITY, 2, ecphValues)) {
        Serial.print("  EC: "); Serial.print(ecphValues[0]); 
        Serial.print(" us/cm  |  PH: "); Serial.println(ecphValues[1] / 10.0, 1);
        Serial.println("  ✓ OK\n");
    } else {
        Serial.println("  ✗ FAILED\n");
    }
    
    delay(500);
    
    // ----------------------------------------------------------------
    // TEST 3: Temperatura + Umidità
    // ----------------------------------------------------------------
    Serial.println("▶ [3/3] Temp + Humidity (2 regs)");
    uint16_t temphumValues[2];
    if (readModbusRegisters(REG_HUMIDITY, 2, temphumValues)) {
        Serial.print("  Humidity: "); Serial.print(temphumValues[0] / 10.0, 1); 
        Serial.print(" %RH  |  Temp: "); Serial.print(temphumValues[1] / 10.0, 1); Serial.println(" °C");
        Serial.println("  ✓ OK\n");
    } else {
        Serial.println("  ✗ FAILED\n");
    }
    
    soilSensorPowerOff();
    
    Serial.println("════════════════════════════════════════════════════════════");
    Serial.println("Next test in 10 seconds...\n\n");
    delay(10000);
}

// ====================================================================
// FINE SKETCH DI TEST
// ====================================================================
