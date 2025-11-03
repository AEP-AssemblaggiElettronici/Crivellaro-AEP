/*
 * ====================================================================
 * SANITIZER SLAVE CONTROLLER v2.2.0
 * ====================================================================
 * Version: 2.2.0
 * Date: 2025-10-16
 * 
 * IMPORTANTE: Questo è il codice SLAVE aggiornato
 * Caricare su tutti gli Arduino Slave (indirizzi 1-16)
 * Compatibile con Master v3.5.0
 * 
 * CHANGELOG:
 * v2.2.0 (2025-10-16):
 * - Aggiunto debounce sensori 1s (HIGH e LOW)
 * - Aggiunta modalità TEST (attivazione immediata LED+valvola)
 * - Aggiunta sequenza PURGE automatica (CH1 3s → pausa 1s → CH2 3s)
 * - Nuovi comandi: TESTMODE:0/1, PURGE
 * - Pre-warning rimane 10s in modalità operativa
 * 
 * v2.1.2 (2025-10-15):
 * - Pin relay scambiati: CH1=D6, CH2=D5
 * - Pre-warning esteso a 10s
 * 
 * PIN MAPPING:
 * D6  -> Relay 1 (CH1)
 * D5  -> Relay 2 (CH2)
 * D4  -> LED 1
 * A5  -> LED 2
 * D2  -> Sensor 1
 * D3  -> Sensor 2
 * D7  -> RS485 TX
 * D8  -> RS485 RX
 * D10 -> RS485 DE/RE
 * 
 * ====================================================================
 */

#include <SoftwareSerial.h>

// ============= PIN DEFINITIONS =============
const int RELAY_1_PIN = 6;        // D6
const int RELAY_2_PIN = 5;        // D5
const int LED_1_PIN = 4;
const int LED_2_PIN = A5;
const int DEBUG_LED_PIN = 9;
const int SENSOR_1_PIN = 2;
const int SENSOR_2_PIN = 3;
const int RS485_TX_PIN = 7;
const int RS485_RX_PIN = 8;
const int RS485_DE_RE = 10;

const int ADDR_BIT_0 = A0;
const int ADDR_BIT_1 = A1;
const int ADDR_BIT_2 = A2;
const int ADDR_BIT_3 = A3;
const int ADDR_BIT_4 = A4;

// ============= CONSTANTS =============
const unsigned long SENSOR_DEBOUNCE_TIME = 1000;   // 1s debounce
const unsigned long LED_PREWARNING_TIME = 10000;   // 10s pre-warning
const unsigned long LED_BLINK_INTERVAL = 500;
const unsigned long BLINK_INTERVAL_START = 500;
const unsigned long BLINK_INTERVAL_END = 50;

// Purge sequence constants
const unsigned long PURGE_VALVE_TIME = 3000;       // 3s valvola
const unsigned long PURGE_PAUSE_TIME = 1000;       // 1s pausa
const unsigned long PURGE_TOTAL_TIME = 7500;       // totale sequenza

// ============= STATE MACHINE =============
enum ChannelState {
  STATE_IDLE,
  STATE_LED_PREWARNING,
  STATE_VALVE_ACTIVE,
  STATE_LED_ON_WAITING,
  STATE_PURGE_SEQUENCE
};

enum PurgePhase {
  PURGE_CH1_ON,
  PURGE_CH1_OFF,
  PURGE_PAUSE,
  PURGE_CH2_ON,
  PURGE_CH2_OFF,
  PURGE_COMPLETE
};

struct Channel {
  ChannelState state;
  unsigned long stateStartTime;
  unsigned long valveDuration;
  int relayPin;
  int ledPin;
  int sensorPin;
  bool ledBlinkState;
  unsigned long lastLedToggle;
  bool sensorHigh;
  
  // Debounce variables
  bool lastRawReading;
  bool currentRawReading;
  unsigned long debounceStartTime;
  bool debouncing;
  bool targetState;
};

// ============= GLOBAL VARIABLES =============
SoftwareSerial rs485(RS485_RX_PIN, RS485_TX_PIN);

byte deviceAddress = 0;
Channel channel1, channel2;
String commandBuffer = "";
const int MAX_COMMAND_LENGTH = 64;

bool testModeActive = false;
bool purgeActive = false;
PurgePhase purgePhase = PURGE_COMPLETE;
unsigned long purgePhaseStartTime = 0;

// ============= FUNCTION PROTOTYPES =============
byte readDeviceAddress();
void initializeChannel(Channel &ch, int relayPin, int ledPin, int sensorPin);
void updateChannel(Channel &ch);
void updateSensorWithDebounce(Channel &ch);
void updateChannelLED(Channel &ch);
void updateTestMode(Channel &ch);
unsigned long calculateBlinkInterval(unsigned long elapsedTime, unsigned long totalTime);
void activateChannel(Channel &ch, int valveTime);
void processCommand(const String &cmd);
void sendResponse(const String &response);
void sendStatusResponse();
void blinkTrafficLed();
void startPurgeSequence();
void updatePurgeSequence();
void stopPurgeSequence();

// ============= SETUP =============
void setup() {
  Serial.begin(115200);
  rs485.begin(9600);
  
  pinMode(RELAY_1_PIN, OUTPUT);
  pinMode(RELAY_2_PIN, OUTPUT);
  pinMode(LED_1_PIN, OUTPUT);
  pinMode(LED_2_PIN, OUTPUT);
  pinMode(DEBUG_LED_PIN, OUTPUT);
  pinMode(RS485_DE_RE, OUTPUT);
  
  pinMode(SENSOR_1_PIN, INPUT);
  pinMode(SENSOR_2_PIN, INPUT);
  pinMode(ADDR_BIT_0, INPUT_PULLUP);
  pinMode(ADDR_BIT_1, INPUT_PULLUP);
  pinMode(ADDR_BIT_2, INPUT_PULLUP);
  pinMode(ADDR_BIT_3, INPUT_PULLUP);
  pinMode(ADDR_BIT_4, INPUT_PULLUP);
  
  digitalWrite(RS485_DE_RE, LOW);
  digitalWrite(RELAY_1_PIN, LOW);
  digitalWrite(RELAY_2_PIN, LOW);
  digitalWrite(LED_1_PIN, LOW);
  digitalWrite(LED_2_PIN, LOW);
  digitalWrite(DEBUG_LED_PIN, LOW);
  
  deviceAddress = readDeviceAddress();
  
  initializeChannel(channel1, RELAY_1_PIN, LED_1_PIN, SENSOR_1_PIN);
  initializeChannel(channel2, RELAY_2_PIN, LED_2_PIN, SENSOR_2_PIN);
  
  delay(500);
  
  Serial.println(F("========================================"));
  Serial.println(F("  SLAVE v2.2.0"));
  Serial.println(F("========================================"));
  Serial.print(F("[INIT] Address: "));
  Serial.println(deviceAddress);
  Serial.println(F("[INIT] CH1 Relay: D6, CH2 Relay: D5"));
  Serial.println(F("[INIT] Sensor debounce: 1s"));
  Serial.println(F("[INIT] Pre-warning: 10s (normal mode)"));
  Serial.println(F("[INIT] Purge: CH1(3s) → pause(1s) → CH2(3s)"));
  Serial.println(F("[INIT] Ready"));
  Serial.println();
  
  for (int i = 0; i < 3; i++) {
    digitalWrite(DEBUG_LED_PIN, HIGH);
    delay(100);
    digitalWrite(DEBUG_LED_PIN, LOW);
    delay(100);
  }
}

// ============= MAIN LOOP =============
void loop() {
  if (purgeActive) {
    updatePurgeSequence();
  } else {
    updateSensorWithDebounce(channel1);
    updateSensorWithDebounce(channel2);
    
    if (testModeActive) {
      updateTestMode(channel1);
      updateTestMode(channel2);
    } else {
      updateChannel(channel1);
      updateChannel(channel2);
    }
  }
  
  while (rs485.available()) {
    char c = rs485.read();
    blinkTrafficLed();
    
    if (c == '\n') {
      if (commandBuffer.length() > 0) {
        processCommand(commandBuffer);
        commandBuffer = "";
      }
    } else {
      commandBuffer += c;
      if (commandBuffer.length() >= MAX_COMMAND_LENGTH) {
        commandBuffer = "";
      }
    }
  }
}

// ============= FUNCTIONS =============
byte readDeviceAddress() {
  byte address = 0;
  if (digitalRead(ADDR_BIT_0) == LOW) address |= 0b00001;
  if (digitalRead(ADDR_BIT_1) == LOW) address |= 0b00010;
  if (digitalRead(ADDR_BIT_2) == LOW) address |= 0b00100;
  if (digitalRead(ADDR_BIT_3) == LOW) address |= 0b01000;
  if (digitalRead(ADDR_BIT_4) == LOW) address |= 0b10000;
  if (address == 0) address = 1;
  return address;
}

void blinkTrafficLed() {
  digitalWrite(DEBUG_LED_PIN, HIGH);
  delay(2);
  digitalWrite(DEBUG_LED_PIN, LOW);
}

void initializeChannel(Channel &ch, int relayPin, int ledPin, int sensorPin) {
  ch.state = STATE_IDLE;
  ch.stateStartTime = 0;
  ch.valveDuration = 0;
  ch.relayPin = relayPin;
  ch.ledPin = ledPin;
  ch.sensorPin = sensorPin;
  ch.ledBlinkState = false;
  ch.lastLedToggle = 0;
  ch.sensorHigh = false;
  ch.lastRawReading = false;
  ch.currentRawReading = false;
  ch.debounceStartTime = 0;
  ch.debouncing = false;
  ch.targetState = false;
}

void updateSensorWithDebounce(Channel &ch) {
  ch.currentRawReading = digitalRead(ch.sensorPin);
  unsigned long currentTime = millis();
  
  // Rileva cambio di stato del sensore
  if (ch.currentRawReading != ch.lastRawReading) {
    // Inizia il debounce
    ch.debouncing = true;
    ch.targetState = ch.currentRawReading;
    ch.debounceStartTime = currentTime;
    ch.lastRawReading = ch.currentRawReading;
  }
  
  // Verifica se il debounce è completato
  if (ch.debouncing) {
    if (currentTime - ch.debounceStartTime >= SENSOR_DEBOUNCE_TIME) {
      // Debounce completato, aggiorna lo stato del sensore
      if (ch.targetState != ch.sensorHigh) {
        ch.sensorHigh = ch.targetState;
        ch.debouncing = false;
        
        Serial.print(F("[SENSOR] CH"));
        Serial.print(ch.sensorPin == SENSOR_1_PIN ? 1 : 2);
        Serial.print(ch.sensorHigh ? F(" DETECTED") : F(" REMOVED"));
        Serial.println();
      }
    }
  }
}

unsigned long calculateBlinkInterval(unsigned long elapsedTime, unsigned long totalTime) {
  float progress = (float)elapsedTime / (float)totalTime;
  if (progress > 1.0) progress = 1.0;
  unsigned long interval = BLINK_INTERVAL_START - 
                          (unsigned long)(progress * (BLINK_INTERVAL_START - BLINK_INTERVAL_END));
  return interval;
}

void updateChannelLED(Channel &ch) {
  unsigned long currentTime = millis();
  
  if (ch.sensorHigh) {
    if (ch.state == STATE_IDLE) {
      if (currentTime - ch.lastLedToggle >= LED_BLINK_INTERVAL) {
        ch.ledBlinkState = !ch.ledBlinkState;
        digitalWrite(ch.ledPin, ch.ledBlinkState);
        ch.lastLedToggle = currentTime;
      }
    } else if (ch.state == STATE_LED_PREWARNING) {
      unsigned long elapsed = currentTime - ch.stateStartTime;
      unsigned long blinkInterval = calculateBlinkInterval(elapsed, LED_PREWARNING_TIME);
      if (currentTime - ch.lastLedToggle >= blinkInterval) {
        ch.ledBlinkState = !ch.ledBlinkState;
        digitalWrite(ch.ledPin, ch.ledBlinkState);
        ch.lastLedToggle = currentTime;
      }
    } else {
      digitalWrite(ch.ledPin, HIGH);
      ch.ledBlinkState = true;
    }
  } else {
    digitalWrite(ch.ledPin, LOW);
    ch.ledBlinkState = false;
  }
}

void updateChannel(Channel &ch) {
  unsigned long currentTime = millis();
  unsigned long elapsedTime = currentTime - ch.stateStartTime;
  
  updateChannelLED(ch);
  
  switch (ch.state) {
    case STATE_IDLE:
      break;
      
    case STATE_LED_PREWARNING:
      if (elapsedTime >= LED_PREWARNING_TIME) {
        digitalWrite(ch.relayPin, HIGH);
        ch.state = STATE_VALVE_ACTIVE;
        ch.stateStartTime = currentTime;
        Serial.print(F("[STATE] CH"));
        Serial.print(ch.sensorPin == SENSOR_1_PIN ? 1 : 2);
        Serial.println(F(" VALVE ON"));
      }
      break;
      
    case STATE_VALVE_ACTIVE:
      if (elapsedTime >= ch.valveDuration) {
        digitalWrite(ch.relayPin, LOW);
        ch.state = STATE_LED_ON_WAITING;
        ch.stateStartTime = currentTime;
        Serial.print(F("[STATE] CH"));
        Serial.print(ch.sensorPin == SENSOR_1_PIN ? 1 : 2);
        Serial.println(F(" VALVE OFF"));
      }
      break;
      
    case STATE_LED_ON_WAITING:
      if (!ch.sensorHigh) {
        ch.state = STATE_IDLE;
        Serial.print(F("[STATE] CH"));
        Serial.print(ch.sensorPin == SENSOR_1_PIN ? 1 : 2);
        Serial.println(F(" IDLE"));
      }
      break;
      
    case STATE_PURGE_SEQUENCE:
      // Gestito da updatePurgeSequence()
      break;
  }
}

void updateTestMode(Channel &ch) {
  // In test mode: attivazione immediata LED + valvola quando sensore HIGH
  if (ch.sensorHigh) {
    digitalWrite(ch.ledPin, HIGH);
    digitalWrite(ch.relayPin, HIGH);
  } else {
    digitalWrite(ch.ledPin, LOW);
    digitalWrite(ch.relayPin, LOW);
  }
}

void activateChannel(Channel &ch, int valveTime) {
  ch.state = STATE_LED_PREWARNING;
  ch.stateStartTime = millis();
  ch.valveDuration = valveTime;
  ch.lastLedToggle = millis();
  
  Serial.print(F("[ACT] CH"));
  Serial.print(ch.sensorPin == SENSOR_1_PIN ? 1 : 2);
  Serial.println(F(" LED accelerating 10s"));
}

void startPurgeSequence() {
  purgeActive = true;
  purgePhase = PURGE_CH1_ON;
  purgePhaseStartTime = millis();
  
  // Spegni tutto prima di iniziare
  digitalWrite(RELAY_1_PIN, LOW);
  digitalWrite(RELAY_2_PIN, LOW);
  digitalWrite(LED_1_PIN, LOW);
  digitalWrite(LED_2_PIN, LOW);
  
  // Inizia con CH1
  digitalWrite(RELAY_1_PIN, HIGH);
  digitalWrite(LED_1_PIN, HIGH);
  
  Serial.println(F("[PURGE] START - CH1 ON (3s)"));
}

void updatePurgeSequence() {
  if (!purgeActive) return;
  
  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - purgePhaseStartTime;
  
  switch (purgePhase) {
    case PURGE_CH1_ON:
      if (elapsed >= PURGE_VALVE_TIME) {
        digitalWrite(RELAY_1_PIN, LOW);
        digitalWrite(LED_1_PIN, LOW);
        purgePhase = PURGE_PAUSE;
        purgePhaseStartTime = currentTime;
        Serial.println(F("[PURGE] CH1 OFF - PAUSE (1s)"));
      }
      break;
      
    case PURGE_PAUSE:
      if (elapsed >= PURGE_PAUSE_TIME) {
        digitalWrite(RELAY_2_PIN, HIGH);
        digitalWrite(LED_2_PIN, HIGH);
        purgePhase = PURGE_CH2_ON;
        purgePhaseStartTime = currentTime;
        Serial.println(F("[PURGE] CH2 ON (3s)"));
      }
      break;
      
    case PURGE_CH2_ON:
      if (elapsed >= PURGE_VALVE_TIME) {
        digitalWrite(RELAY_2_PIN, LOW);
        digitalWrite(LED_2_PIN, LOW);
        purgePhase = PURGE_COMPLETE;
        purgePhaseStartTime = currentTime;
        Serial.println(F("[PURGE] COMPLETE"));
        stopPurgeSequence();
      }
      break;
      
    default:
      break;
  }
}

void stopPurgeSequence() {
  purgeActive = false;
  purgePhase = PURGE_COMPLETE;
  digitalWrite(RELAY_1_PIN, LOW);
  digitalWrite(RELAY_2_PIN, LOW);
  digitalWrite(LED_1_PIN, LOW);
  digitalWrite(LED_2_PIN, LOW);
}

void processCommand(const String &cmd) {
  if (cmd.charAt(0) != '>') return;
  if (cmd.length() < 4) return;
  
  int addr = cmd.substring(1, 3).toInt();
  if (addr != deviceAddress) return;
  
  int firstColon = cmd.indexOf(':', 1);
  if (firstColon == -1) return;
  
  int secondColon = cmd.indexOf(':', firstColon + 1);
  String cmdType;
  
  if (secondColon == -1) {
    cmdType = cmd.substring(firstColon + 1);
  } else {
    cmdType = cmd.substring(firstColon + 1, secondColon);
  }
  
  if (cmdType == "POLL") {
    sendStatusResponse();
    
  } else if (cmdType == "TESTMODE") {
    if (secondColon == -1) return;
    String params = cmd.substring(secondColon + 1);
    int mode = params.toInt();
    
    testModeActive = (mode == 1);
    
    if (testModeActive) {
      Serial.println(F("[MODE] TEST ACTIVATED"));
      // Resetta gli stati
      channel1.state = STATE_IDLE;
      channel2.state = STATE_IDLE;
      digitalWrite(RELAY_1_PIN, LOW);
      digitalWrite(RELAY_2_PIN, LOW);
      digitalWrite(LED_1_PIN, LOW);
      digitalWrite(LED_2_PIN, LOW);
    } else {
      Serial.println(F("[MODE] NORMAL ACTIVATED"));
      digitalWrite(RELAY_1_PIN, LOW);
      digitalWrite(RELAY_2_PIN, LOW);
      digitalWrite(LED_1_PIN, LOW);
      digitalWrite(LED_2_PIN, LOW);
    }
    
    sendResponse("<" + String(deviceAddress < 10 ? "0" : "") + 
                 String(deviceAddress) + ":ACK\n");
    
  } else if (cmdType == "PURGE") {
    startPurgeSequence();
    sendResponse("<" + String(deviceAddress < 10 ? "0" : "") + 
                 String(deviceAddress) + ":ACK\n");
    
  } else if (cmdType == "ACT") {
    if (secondColon == -1) return;
    
    String params = cmd.substring(secondColon + 1);
    int channel = 0;
    int valveTime = 0;
    
    int firstComma = params.indexOf(',');
    int secondComma = params.indexOf(',', firstComma + 1);
    
    if (firstComma != -1 && secondComma != -1) {
      channel = params.substring(0, firstComma).toInt();
      valveTime = params.substring(firstComma + 1, secondComma).toInt();
      
      if (channel == 1) {
        activateChannel(channel1, valveTime);
        sendResponse("<" + String(deviceAddress < 10 ? "0" : "") + 
                     String(deviceAddress) + ":ACK\n");
      } else if (channel == 2) {
        activateChannel(channel2, valveTime);
        sendResponse("<" + String(deviceAddress < 10 ? "0" : "") + 
                     String(deviceAddress) + ":ACK\n");
      }
    }
    
  } else if (cmdType == "RESET") {
    channel1.state = STATE_IDLE;
    channel2.state = STATE_IDLE;
    testModeActive = false;
    stopPurgeSequence();
    digitalWrite(RELAY_1_PIN, LOW);
    digitalWrite(RELAY_2_PIN, LOW);
    digitalWrite(LED_1_PIN, LOW);
    digitalWrite(LED_2_PIN, LOW);
    sendResponse("<" + String(deviceAddress < 10 ? "0" : "") + 
                 String(deviceAddress) + ":ACK\n");
  }
}

void sendResponse(const String &response) {
  digitalWrite(RS485_DE_RE, HIGH);
  delayMicroseconds(50);
  blinkTrafficLed();
  rs485.print(response);
  rs485.flush();
  delayMicroseconds(50);
  digitalWrite(RS485_DE_RE, LOW);
}

void sendStatusResponse() {
  String response = "<";
  if (deviceAddress < 10) response += "0";
  response += String(deviceAddress);
  response += ":STATUS:";
  response += String(channel1.sensorHigh ? 1 : 0);
  response += ",";
  response += String(channel2.sensorHigh ? 1 : 0);
  response += ",";
  response += String(channel1.state == STATE_VALVE_ACTIVE ? 1 : 0);
  response += ",";
  response += String(channel2.state == STATE_VALVE_ACTIVE ? 1 : 0);
  response += ",";
  response += String(testModeActive ? 1 : 0);
  response += ",";
  response += String(purgeActive ? 1 : 0);
  response += "\n";
  sendResponse(response);
}

/*
 * END CODE v2.2.0
 */