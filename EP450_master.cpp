/*
 * ====================================================================
 * SANITIZER MASTER CONTROLLER v3.5.1
 * ====================================================================
 * Version: 3.5.1
 * Date: 2025-10-16
 * 
 * IMPORTANTE: Questo è il codice MASTER aggiornato
 * Caricare su Arduino Master (indirizzo 0)
 * Compatibile con Slave v2.2.0
 * 
 * CHANGELOG:
 * v3.5.1 (2025-10-16):
 * - Nuova logica sicurezza presenza umana:
 *   • Se presenza umana al momento della sanificazione → LED lampeggia, attesa
 *   • Quando presenza sparisce → timer 10s
 *   • Dopo 10s senza presenza → sanificazione parte
 *   • Se presenza torna durante i 10s → timer resetta
 *   • Se carrello rimosso durante attesa → reset completo
 * 
 * v3.5.0 (2025-10-16):
 * - Timer carrello ridotto a 4s (era 10s)
 * - Pompa parte a 4s (era 10s)
 * - Test mode: invia TESTMODE:1/0 agli slave, pompa disattivata
 * - Purge mode: sequenza controllata con comando PURGE
 * - Purge: accende pompa, invia PURGE a ogni slave, aspetta 8s, passa al successivo
 * - Ciclo completo: ~16.5s (1s + 4s + 10s + 1.5s)
 * 
 * v3.4.1 (2025-10-15):
 * - Timer ridotto a 10s
 * - Pompa parte a 10s
 * - ACT inviato a 10s
 * 
 * ====================================================================
 */

#include <SoftwareSerial.h>

// ============= PIN DEFINITIONS =============
const int SAFETY_SENSOR_PIN = 2;
const int LIQUID_LEVEL_PIN = 3;
const int RED_LED_PIN = 4;
const int PUMP_RELAY_PIN = 5;
const int RS485_TX_PIN = 7;
const int RS485_RX_PIN = 8;
const int DEBUG_LED_PIN = 9;
const int RS485_DE_RE = 10;
const int TEST_MODE_PIN = A0;
const int PURGE_MODE_PIN = A1;

// ============= CONSTANTS =============
const int NUM_SLAVES = 16;
const unsigned long SLAVE_TIMEOUT = 300;
const unsigned long POLL_INTERVAL = 100;
const int MAX_RESPONSE_LENGTH = 64;
const unsigned long CART_DETECTION_TIME = 4000;    // 4 seconds (was 10s)
const unsigned long PUMP_PREHEAT_TIME = 4000;      // 4 seconds (was 10s)
const unsigned long VALVE_DURATION = 1500;
const unsigned long LED_POST_DURATION = 0;
const unsigned long RED_LED_BLINK_INTERVAL = 500;
const unsigned long SLAVE_PREWARNING = 10000;      // 10s slave LED pre-warning
const unsigned long TOTAL_PUMP_TIME = SLAVE_PREWARNING + VALVE_DURATION;  // 11.5s pump active
const unsigned long PURGE_SLAVE_INTERVAL = 8000;   // 8s between slaves during purge
const unsigned long SAFETY_WAIT_TIME = 10000;      // 10s wait after human presence clears

// ============= STRUCTURES =============
struct SlaveChannel {
  bool sensorHigh;
  unsigned long sensorHighStart;
  bool timerActive;
  bool pumpActive;
  unsigned long pumpStartTime;
  bool sanitizing;
  unsigned long sanitizeStart;
  bool waitingForSafety;
  unsigned long safetyWaitStart;
};

struct SlaveStatus {
  bool online;
  unsigned long lastSeen;
  SlaveChannel channel1;
  SlaveChannel channel2;
};

// ============= GLOBAL VARIABLES =============
SoftwareSerial rs485(RS485_RX_PIN, RS485_TX_PIN);

SlaveStatus slaves[NUM_SLAVES + 1];

unsigned long lastPollTime = 0;
int currentSlaveIndex = 1;
unsigned long commandSentTime = 0;
bool waitingForResponse = false;
String responseBuffer = "";

bool safetyEnabled = true;
bool redLedState = false;
unsigned long lastRedLedToggle = 0;
bool testMode = false;
bool lastTestModeState = false;

bool purgeActive = false;
bool purgeHardware = false;
int purgeCurrentSlaveIndex = 0;
unsigned long purgeSlaveStartTime = 0;
bool lastPurgePinState = false;

// ============= FUNCTION PROTOTYPES =============
void rs485Send(const String& message);
void blinkTrafficLed();
void sendPollCommand(int slaveAddr);
void sendActCommand(int slaveAddr, int channel, int valveTime, int ledTime);
void sendTestModeCommand(int slaveAddr, bool enable);
void sendPurgeCommand(int slaveAddr);
void processResponse(const String& response);
void parseStatusResponse(int addr, const String& data);
void updateTimers();
void updateRedLed();
void updateTestMode();
void updatePurgeMode();
void startPurge();
void updatePurgeSequence();
void stopPurge();
int findNextOnlineSlave(int startFrom);
void processSerialCommand();
void printHelp();
void printSystemInfo();
void printAllSlavesStatus();
void printActiveTimers();

// ============= SETUP =============
void setup() {
  Serial.begin(115200);
  rs485.begin(9600);

  pinMode(SAFETY_SENSOR_PIN, INPUT_PULLUP);
  pinMode(LIQUID_LEVEL_PIN, INPUT_PULLUP);
  pinMode(TEST_MODE_PIN, INPUT_PULLUP);
  pinMode(PURGE_MODE_PIN, INPUT_PULLUP);
  pinMode(PUMP_RELAY_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(DEBUG_LED_PIN, OUTPUT);
  pinMode(RS485_DE_RE, OUTPUT);

  digitalWrite(RS485_DE_RE, LOW);
  digitalWrite(PUMP_RELAY_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(DEBUG_LED_PIN, LOW);

  for (int i = 1; i <= NUM_SLAVES; i++) {
    slaves[i].online = false;
    slaves[i].lastSeen = 0;
    slaves[i].channel1 = { false, 0, false, false, 0, false, 0, false, 0 };
    slaves[i].channel2 = { false, 0, false, false, 0, false, 0, false, 0 };
  }

  delay(1000);

  testMode = digitalRead(TEST_MODE_PIN);
  lastTestModeState = testMode;
  lastPurgePinState = digitalRead(PURGE_MODE_PIN);

  Serial.println(F("========================================"));
  Serial.println(F("  SANITIZER MASTER v3.5.1"));
  Serial.println(F("========================================"));
  Serial.println(F("[INIT] Master initialized (Address: 0)"));
  Serial.println(F("[INIT] Cart detection: 4s timer (+ 1s debounce slave)"));
  Serial.println(F("[INIT] Pump: starts at 4s mark"));
  Serial.println(F("[INIT] Slave pre-warning: 10s"));
  Serial.println(F("[INIT] Valve duration: 1.5s"));
  Serial.println(F("[INIT] Safety wait: 10s after human presence"));
  Serial.println(F("[INIT] Total cycle: ~16.5s"));
  Serial.println(F("[INIT] Type 'HELP' for commands"));
  Serial.println();
}

// ============= MAIN LOOP =============
void loop() {
  unsigned long currentTime = millis();

  processSerialCommand();
  updateTestMode();
  updatePurgeMode();
  updateRedLed();

  if (purgeActive) {
    updatePurgeSequence();
    return;
  }

  if (!testMode) {
    updateTimers();
  }

  if (waitingForResponse) {
    while (rs485.available()) {
      char c = rs485.read();
      blinkTrafficLed();

      if (c == '\n') {
        processResponse(responseBuffer);
        responseBuffer = "";
        waitingForResponse = false;
        delay(10);
        currentSlaveIndex++;
        if (currentSlaveIndex > NUM_SLAVES) currentSlaveIndex = 1;
        lastPollTime = millis();
      } else {
        responseBuffer += c;
        if (responseBuffer.length() >= MAX_RESPONSE_LENGTH) {
          responseBuffer = "";
          waitingForResponse = false;
        }
      }
    }

    if (currentTime - commandSentTime > SLAVE_TIMEOUT) {
      if (slaves[currentSlaveIndex].online) {
        slaves[currentSlaveIndex].online = false;
      }
      waitingForResponse = false;
      responseBuffer = "";
      currentSlaveIndex++;
      if (currentSlaveIndex > NUM_SLAVES) currentSlaveIndex = 1;
      lastPollTime = millis();
    }
  } else {
    if (currentTime - lastPollTime >= POLL_INTERVAL) {
      sendPollCommand(currentSlaveIndex);
      lastPollTime = currentTime;
    }
  }
}

// ============= RS485 FUNCTIONS =============
void rs485Send(const String& message) {
  digitalWrite(RS485_DE_RE, HIGH);
  delayMicroseconds(50);
  blinkTrafficLed();
  rs485.print(message);
  rs485.flush();
  delayMicroseconds(50);
  digitalWrite(RS485_DE_RE, LOW);
}

void blinkTrafficLed() {
  digitalWrite(DEBUG_LED_PIN, HIGH);
  delay(2);
  digitalWrite(DEBUG_LED_PIN, LOW);
}

void sendPollCommand(int slaveAddr) {
  String command = ">";
  if (slaveAddr < 10) command += "0";
  command += String(slaveAddr);
  command += ":POLL\n";

  rs485Send(command);
  commandSentTime = millis();
  waitingForResponse = true;
  responseBuffer = "";
}

void sendActCommand(int slaveAddr, int channel, int valveTime, int ledTime) {
  String command = ">";
  if (slaveAddr < 10) command += "0";
  command += String(slaveAddr);
  command += ":ACT:";
  command += String(channel);
  command += ",";
  command += String(valveTime);
  command += ",";
  command += String(ledTime);
  command += "\n";

  rs485Send(command);
  delay(50);
}

void sendTestModeCommand(int slaveAddr, bool enable) {
  String command = ">";
  if (slaveAddr < 10) command += "0";
  command += String(slaveAddr);
  command += ":TESTMODE:";
  command += String(enable ? 1 : 0);
  command += "\n";

  rs485Send(command);
  delay(50);
}

void sendPurgeCommand(int slaveAddr) {
  String command = ">";
  if (slaveAddr < 10) command += "0";
  command += String(slaveAddr);
  command += ":PURGE\n";

  rs485Send(command);
  delay(50);
}

void processResponse(const String& response) {
  if (response.length() < 4 || response.charAt(0) != '<') return;

  int addr = response.substring(1, 3).toInt();
  if (addr < 1 || addr > NUM_SLAVES) return;

  int firstColon = response.indexOf(':', 1);
  if (firstColon == -1) return;

  int secondColon = response.indexOf(':', firstColon + 1);
  String cmdType = response.substring(firstColon + 1, secondColon);

  bool wasOffline = !slaves[addr].online;
  slaves[addr].online = true;
  slaves[addr].lastSeen = millis();

  if (wasOffline) {
    Serial.print(F("[INFO] Slave "));
    if (addr < 10) Serial.print("0");
    Serial.print(addr);
    Serial.println(F(" ONLINE"));
  }

  if (cmdType == "STATUS") {
    String data = response.substring(secondColon + 1);
    parseStatusResponse(addr, data);
  }
}

void parseStatusResponse(int addr, const String& data) {
  int values[6] = { 0 };
  int valueIndex = 0;
  int startPos = 0;

  for (int i = 0; i <= data.length(); i++) {
    if (i == data.length() || data.charAt(i) == ',') {
      if (valueIndex < 6) {
        values[valueIndex] = data.substring(startPos, i).toInt();
        valueIndex++;
      }
      startPos = i + 1;
    }
  }

  if (valueIndex == 6) {
    bool sensor1 = values[0];
    bool sensor2 = values[1];

    if (sensor1 && !slaves[addr].channel1.sensorHigh) {
      slaves[addr].channel1.sensorHigh = true;
      slaves[addr].channel1.sensorHighStart = millis();
      slaves[addr].channel1.timerActive = true;
      slaves[addr].channel1.waitingForSafety = false;
      slaves[addr].channel1.safetyWaitStart = 0;
      Serial.print(F("[DETECT] Slave "));
      if (addr < 10) Serial.print("0");
      Serial.print(addr);
      Serial.println(F(" CH1 - Cart detected (after debounce)"));
    } else if (!sensor1 && slaves[addr].channel1.sensorHigh) {
      slaves[addr].channel1.sensorHigh = false;
      slaves[addr].channel1.timerActive = false;
      slaves[addr].channel1.sanitizing = false;
      slaves[addr].channel1.waitingForSafety = false;
      slaves[addr].channel1.safetyWaitStart = 0;
      Serial.print(F("[REMOVE] Slave "));
      if (addr < 10) Serial.print("0");
      Serial.print(addr);
      Serial.println(F(" CH1 - Cart removed"));
    }

    if (sensor2 && !slaves[addr].channel2.sensorHigh) {
      slaves[addr].channel2.sensorHigh = true;
      slaves[addr].channel2.sensorHighStart = millis();
      slaves[addr].channel2.timerActive = true;
      slaves[addr].channel2.waitingForSafety = false;
      slaves[addr].channel2.safetyWaitStart = 0;
      Serial.print(F("[DETECT] Slave "));
      if (addr < 10) Serial.print("0");
      Serial.print(addr);
      Serial.println(F(" CH2 - Cart detected (after debounce)"));
    } else if (!sensor2 && slaves[addr].channel2.sensorHigh) {
      slaves[addr].channel2.sensorHigh = false;
      slaves[addr].channel2.timerActive = false;
      slaves[addr].channel2.sanitizing = false;
      slaves[addr].channel2.waitingForSafety = false;
      slaves[addr].channel2.safetyWaitStart = 0;
      Serial.print(F("[REMOVE] Slave "));
      if (addr < 10) Serial.print("0");
      Serial.print(addr);
      Serial.println(F(" CH2 - Cart removed"));
    }
  }
}

// ============= TIMER MANAGEMENT =============
void updateTimers() {
  unsigned long currentTime = millis();
  bool humanPresent = safetyEnabled && digitalRead(SAFETY_SENSOR_PIN);
  bool anyPumpActive = false;

  for (int i = 1; i <= NUM_SLAVES; i++) {
    if (!slaves[i].online) continue;

    // ============= CHANNEL 1 =============
    
    // Fase 1: Timer iniziale (4s dopo rilevamento carrello)
    if (slaves[i].channel1.timerActive && !slaves[i].channel1.pumpActive && 
        !slaves[i].channel1.sanitizing && !slaves[i].channel1.waitingForSafety) {
      unsigned long elapsed = currentTime - slaves[i].channel1.sensorHighStart;
      
      if (elapsed >= PUMP_PREHEAT_TIME) {
        if (!humanPresent) {
          // Nessuna presenza umana, parte la sanificazione
          digitalWrite(PUMP_RELAY_PIN, HIGH);
          Serial.print(F("[PUMP] ON for Slave "));
          if (i < 10) Serial.print("0");
          Serial.print(i);
          Serial.println(F(" CH1"));
          
          sendActCommand(i, 1, VALVE_DURATION, LED_POST_DURATION);
          slaves[i].channel1.pumpActive = true;
          slaves[i].channel1.sanitizing = true;
          slaves[i].channel1.sanitizeStart = currentTime;
          slaves[i].channel1.timerActive = false;
        } else {
          // Presenza umana rilevata, entra in attesa sicurezza
          Serial.print(F("[SAFETY] Slave "));
          if (i < 10) Serial.print("0");
          Serial.print(i);
          Serial.println(F(" CH1 - Human present, waiting..."));
          
          slaves[i].channel1.timerActive = false;
          slaves[i].channel1.waitingForSafety = true;
          slaves[i].channel1.safetyWaitStart = 0;
        }
      }
    }
    
    // Fase 2: Attesa sicurezza (presenza umana)
    if (slaves[i].channel1.waitingForSafety && !slaves[i].channel1.sanitizing) {
      if (humanPresent) {
        // Presenza umana ancora rilevata, reset timer
        slaves[i].channel1.safetyWaitStart = 0;
      } else {
        // Nessuna presenza umana
        if (slaves[i].channel1.safetyWaitStart == 0) {
          // Inizia il timer di sicurezza
          slaves[i].channel1.safetyWaitStart = currentTime;
          Serial.print(F("[SAFETY] Slave "));
          if (i < 10) Serial.print("0");
          Serial.print(i);
          Serial.println(F(" CH1 - Human clear, 10s timer started"));
        } else {
          // Controlla se sono passati 10s
          unsigned long safetyElapsed = currentTime - slaves[i].channel1.safetyWaitStart;
          if (safetyElapsed >= SAFETY_WAIT_TIME) {
            // 10s passati, parte la sanificazione
            digitalWrite(PUMP_RELAY_PIN, HIGH);
            Serial.print(F("[PUMP] ON for Slave "));
            if (i < 10) Serial.print("0");
            Serial.print(i);
            Serial.println(F(" CH1 (after safety wait)"));
            
            sendActCommand(i, 1, VALVE_DURATION, LED_POST_DURATION);
            slaves[i].channel1.pumpActive = true;
            slaves[i].channel1.sanitizing = true;
            slaves[i].channel1.sanitizeStart = currentTime;
            slaves[i].channel1.waitingForSafety = false;
            slaves[i].channel1.safetyWaitStart = 0;
          }
        }
      }
    }

    // Fase 3: Sanificazione in corso
    if (slaves[i].channel1.sanitizing) {
      unsigned long elapsed = currentTime - slaves[i].channel1.sanitizeStart;
      
      if (elapsed >= TOTAL_PUMP_TIME) {
        slaves[i].channel1.pumpActive = false;
        slaves[i].channel1.sanitizing = false;
        Serial.print(F("[PUMP] OFF Slave "));
        if (i < 10) Serial.print("0");
        Serial.print(i);
        Serial.println(F(" CH1"));
      } else {
        anyPumpActive = true;
      }
    }

    // ============= CHANNEL 2 =============
    
    // Fase 1: Timer iniziale (4s dopo rilevamento carrello)
    if (slaves[i].channel2.timerActive && !slaves[i].channel2.pumpActive && 
        !slaves[i].channel2.sanitizing && !slaves[i].channel2.waitingForSafety) {
      unsigned long elapsed = currentTime - slaves[i].channel2.sensorHighStart;
      
      if (elapsed >= PUMP_PREHEAT_TIME) {
        if (!humanPresent) {
          // Nessuna presenza umana, parte la sanificazione
          digitalWrite(PUMP_RELAY_PIN, HIGH);
          Serial.print(F("[PUMP] ON for Slave "));
          if (i < 10) Serial.print("0");
          Serial.print(i);
          Serial.println(F(" CH2"));
          
          sendActCommand(i, 2, VALVE_DURATION, LED_POST_DURATION);
          slaves[i].channel2.pumpActive = true;
          slaves[i].channel2.sanitizing = true;
          slaves[i].channel2.sanitizeStart = currentTime;
          slaves[i].channel2.timerActive = false;
        } else {
          // Presenza umana rilevata, entra in attesa sicurezza
          Serial.print(F("[SAFETY] Slave "));
          if (i < 10) Serial.print("0");
          Serial.print(i);
          Serial.println(F(" CH2 - Human present, waiting..."));
          
          slaves[i].channel2.timerActive = false;
          slaves[i].channel2.waitingForSafety = true;
          slaves[i].channel2.safetyWaitStart = 0;
        }
      }
    }
    
    // Fase 2: Attesa sicurezza (presenza umana)
    if (slaves[i].channel2.waitingForSafety && !slaves[i].channel2.sanitizing) {
      if (humanPresent) {
        // Presenza umana ancora rilevata, reset timer
        slaves[i].channel2.safetyWaitStart = 0;
      } else {
        // Nessuna presenza umana
        if (slaves[i].channel2.safetyWaitStart == 0) {
          // Inizia il timer di sicurezza
          slaves[i].channel2.safetyWaitStart = currentTime;
          Serial.print(F("[SAFETY] Slave "));
          if (i < 10) Serial.print("0");
          Serial.print(i);
          Serial.println(F(" CH2 - Human clear, 10s timer started"));
        } else {
          // Controlla se sono passati 10s
          unsigned long safetyElapsed = currentTime - slaves[i].channel2.safetyWaitStart;
          if (safetyElapsed >= SAFETY_WAIT_TIME) {
            // 10s passati, parte la sanificazione
            digitalWrite(PUMP_RELAY_PIN, HIGH);
            Serial.print(F("[PUMP] ON for Slave "));
            if (i < 10) Serial.print("0");
            Serial.print(i);
            Serial.println(F(" CH2 (after safety wait)"));
            
            sendActCommand(i, 2, VALVE_DURATION, LED_POST_DURATION);
            slaves[i].channel2.pumpActive = true;
            slaves[i].channel2.sanitizing = true;
            slaves[i].channel2.sanitizeStart = currentTime;
            slaves[i].channel2.waitingForSafety = false;
            slaves[i].channel2.safetyWaitStart = 0;
          }
        }
      }
    }

    // Fase 3: Sanificazione in corso
    if (slaves[i].channel2.sanitizing) {
      unsigned long elapsed = currentTime - slaves[i].channel2.sanitizeStart;
      
      if (elapsed >= TOTAL_PUMP_TIME) {
        slaves[i].channel2.pumpActive = false;
        slaves[i].channel2.sanitizing = false;
        Serial.print(F("[PUMP] OFF Slave "));
        if (i < 10) Serial.print("0");
        Serial.print(i);
        Serial.println(F(" CH2"));
      } else {
        anyPumpActive = true;
      }
    }
  }

  if (!anyPumpActive) {
    digitalWrite(PUMP_RELAY_PIN, LOW);
  }
}

// ============= RED LED =============
void updateRedLed() {
  bool liquidLow = digitalRead(LIQUID_LEVEL_PIN);
  if (liquidLow) {
    if (millis() - lastRedLedToggle >= RED_LED_BLINK_INTERVAL) {
      redLedState = !redLedState;
      digitalWrite(RED_LED_PIN, redLedState);
      lastRedLedToggle = millis();
    }
  } else {
    digitalWrite(RED_LED_PIN, LOW);
    redLedState = false;
  }
}

// ============= TEST MODE =============
void updateTestMode() {
  bool currentState = digitalRead(TEST_MODE_PIN);
  if (currentState != lastTestModeState) {
    testMode = currentState;
    lastTestModeState = currentState;
    
    if (testMode) {
      Serial.println(F("[TEST] MODE ACTIVATED"));
      Serial.println(F("[TEST] Sending TESTMODE:1 to all slaves..."));
      
      // Invia comando TESTMODE:1 a tutti gli slave online
      for (int i = 1; i <= NUM_SLAVES; i++) {
        if (slaves[i].online) {
          sendTestModeCommand(i, true);
          Serial.print(F("[TEST] Sent to Slave "));
          if (i < 10) Serial.print("0");
          Serial.println(i);
          delay(100);
        }
      }
      
      // Spegni la pompa in test mode
      digitalWrite(PUMP_RELAY_PIN, LOW);
      
      // Reset timers
      for (int i = 1; i <= NUM_SLAVES; i++) {
        slaves[i].channel1.timerActive = false;
        slaves[i].channel1.pumpActive = false;
        slaves[i].channel1.sanitizing = false;
        slaves[i].channel1.waitingForSafety = false;
        slaves[i].channel1.safetyWaitStart = 0;
        slaves[i].channel2.timerActive = false;
        slaves[i].channel2.pumpActive = false;
        slaves[i].channel2.sanitizing = false;
        slaves[i].channel2.waitingForSafety = false;
        slaves[i].channel2.safetyWaitStart = 0;
      }
      
    } else {
      Serial.println(F("[TEST] MODE DEACTIVATED"));
      Serial.println(F("[TEST] Sending TESTMODE:0 to all slaves..."));
      
      // Invia comando TESTMODE:0 a tutti gli slave online
      for (int i = 1; i <= NUM_SLAVES; i++) {
        if (slaves[i].online) {
          sendTestModeCommand(i, false);
          Serial.print(F("[TEST] Sent to Slave "));
          if (i < 10) Serial.print("0");
          Serial.println(i);
          delay(100);
        }
      }
    }
  }
}

// ============= PURGE MODE =============
void updatePurgeMode() {
  bool currentPinState = digitalRead(PURGE_MODE_PIN);
  if (currentPinState != lastPurgePinState) {
    lastPurgePinState = currentPinState;
    if (currentPinState) {
      startPurge();
    } else {
      if (purgeActive) stopPurge();
    }
  }
}

int findNextOnlineSlave(int startFrom) {
  for (int i = startFrom; i <= NUM_SLAVES; i++) {
    if (slaves[i].online) return i;
  }
  return 0;
}

void startPurge() {
  // Trova il primo slave online
  int firstSlave = findNextOnlineSlave(1);
  if (firstSlave == 0) {
    Serial.println(F("[PURGE] No slaves online!"));
    return;
  }

  purgeActive = true;
  purgeHardware = true;
  purgeCurrentSlaveIndex = firstSlave;
  purgeSlaveStartTime = millis();

  // Accendi la pompa
  digitalWrite(PUMP_RELAY_PIN, HIGH);
  
  Serial.println(F("[PURGE] SEQUENCE STARTED"));
  Serial.print(F("[PURGE] Pump ON - Starting with Slave "));
  if (purgeCurrentSlaveIndex < 10) Serial.print("0");
  Serial.println(purgeCurrentSlaveIndex);
  
  // Invia comando PURGE al primo slave
  sendPurgeCommand(purgeCurrentSlaveIndex);
}

void updatePurgeSequence() {
  if (!purgeActive) return;
  
  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - purgeSlaveStartTime;
  
  // Aspetta PURGE_SLAVE_INTERVAL prima di passare al prossimo slave
  if (elapsed >= PURGE_SLAVE_INTERVAL) {
    // Cerca il prossimo slave online
    int nextSlave = findNextOnlineSlave(purgeCurrentSlaveIndex + 1);
    
    if (nextSlave > 0) {
      // C'è un altro slave, continua la sequenza
      purgeCurrentSlaveIndex = nextSlave;
      purgeSlaveStartTime = currentTime;
      
      Serial.print(F("[PURGE] Next slave: "));
      if (purgeCurrentSlaveIndex < 10) Serial.print("0");
      Serial.println(purgeCurrentSlaveIndex);
      
      sendPurgeCommand(purgeCurrentSlaveIndex);
    } else {
      // Nessun altro slave, termina la sequenza
      stopPurge();
    }
  }
}

void stopPurge() {
  if (!purgeActive) return;
  
  digitalWrite(PUMP_RELAY_PIN, LOW);
  
  Serial.println(F("[PURGE] SEQUENCE COMPLETED"));
  Serial.println(F("[PURGE] Pump OFF"));
  
  purgeActive = false;
  purgeHardware = false;
  purgeCurrentSlaveIndex = 0;
}

// ============= SERIAL COMMANDS =============
void processSerialCommand() {
  static String cmdBuffer = "";
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdBuffer.length() > 0) {
        cmdBuffer.trim();
        cmdBuffer.toUpperCase();
        if (cmdBuffer == "HELP") printHelp();
        else if (cmdBuffer == "INFO") printSystemInfo();
        else if (cmdBuffer == "STATUS") printAllSlavesStatus();
        else if (cmdBuffer == "TIMERS") printActiveTimers();
        cmdBuffer = "";
      }
    } else {
      cmdBuffer += c;
    }
  }
}

void printHelp() {
  Serial.println(F("\n=== COMMANDS ==="));
  Serial.println(F("HELP   - Commands"));
  Serial.println(F("INFO   - Status"));
  Serial.println(F("STATUS - Slaves"));
  Serial.println(F("TIMERS - Timers"));
  Serial.println(F("================\n"));
}

void printSystemInfo() {
  Serial.println(F("\n=== SYSTEM ==="));
  Serial.print(F("Test: "));
  Serial.println(testMode ? F("ON") : F("OFF"));
  Serial.print(F("Purge: "));
  Serial.println(purgeActive ? F("ON") : F("OFF"));
  Serial.print(F("Pump: "));
  Serial.println(digitalRead(PUMP_RELAY_PIN) ? F("ON") : F("OFF"));
  Serial.println(F("==============\n"));
}

void printAllSlavesStatus() {
  Serial.println(F("\n=== SLAVES ==="));
  for (int i = 1; i <= NUM_SLAVES; i++) {
    if (slaves[i].online) {
      Serial.print(F("Slave "));
      if (i < 10) Serial.print("0");
      Serial.print(i);
      Serial.print(F(" - CH1:"));
      Serial.print(slaves[i].channel1.sensorHigh ? F("HIGH") : F("LOW"));
      Serial.print(F(" CH2:"));
      Serial.println(slaves[i].channel2.sensorHigh ? F("HIGH") : F("LOW"));
    }
  }
  Serial.println(F("==============\n"));
}

void printActiveTimers() {
  Serial.println(F("\n=== ACTIVE TIMERS ==="));
  bool anyActive = false;
  for (int i = 1; i <= NUM_SLAVES; i++) {
    if (slaves[i].online) {
      if (slaves[i].channel1.timerActive || slaves[i].channel1.sanitizing) {
        Serial.print(F("Slave "));
        if (i < 10) Serial.print("0");
        Serial.print(i);
        Serial.print(F(" CH1: "));
        if (slaves[i].channel1.timerActive) Serial.println(F("WAITING"));
        if (slaves[i].channel1.sanitizing) Serial.println(F("SANITIZING"));
        anyActive = true;
      }
      if (slaves[i].channel2.timerActive || slaves[i].channel2.sanitizing) {
        Serial.print(F("Slave "));
        if (i < 10) Serial.print("0");
        Serial.print(i);
        Serial.print(F(" CH2: "));
        if (slaves[i].channel2.timerActive) Serial.println(F("WAITING"));
        if (slaves[i].channel2.sanitizing) Serial.println(F("SANITIZING"));
        anyActive = true;
      }
    }
  }
  if (!anyActive) {
    Serial.println(F("No active timers"));
  }
  Serial.println(F("====================\n"));
}

/*
 * END CODE v3.5.1
 */