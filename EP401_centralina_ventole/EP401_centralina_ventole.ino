/*******************************************************************************
 * PROJECT: EP401v1 - Automazione Ventole
 * FILE: EP401v1_Automazione_Ventole_Firmware.ino
 * AUTHOR: E.Pacenti
 * TARGET: ESP32-S3
 * 
 * VERSION: 3.0.2
 * DATE: 2025-11-05
 * 
 * DESCRIPTION:
 * Sistema di controllo per 6 ventole EBM-Papst R3G133-RA01-20 con WebServer
 * - 3 canali PWM (2 ventole per canale)
 * - 6 feedback tachimetrici per monitoraggio RPM
 * - 2 sensori PT100 per temperatura (PT101, PT102)
 * - 2 ingressi digitali (pressostato + crepuscolare)
 * - 3 uscite digitali (LED allarme + 2 liberi)
 * - 1 LED indicatore WiFi (LED3)
 * - 1 pulsante attivazione WiFi on-demand
 * - WebServer WiFi Access Point per controllo remoto
 * - Controllo automatico temperatura con isteresi
 * - Modalità crepuscolare con riduzione velocità
 * 
 * HARDWARE:
 * - MCU: ESP32-S3-WROOM
 * - Ventole: EBM-Papst R3G133-RA01-20 (EC, 230VAC)
 * - PWM: Software, 500Hz, risoluzione 100 step (INVERTITO), minimo 30%
 * - PT100: ADC interno, partitore con R_REF=172Ω, range 0-60°C
 * - WiFi: Access Point on-demand "EP401-FAN_CTRL" - IP: 192.100.100.1
 * - WiFi Button: Pin 7 (attivazione/mantenimento)
 * - WiFi LED: Pin 6 (indicatore stato)
 * 
 * CHANGELOG:
 * v3.0.2 (2025-11-05) - WiFi On-Demand con timeout automatico
 *   + Aggiunto pulsante WiFi su pin 7 per attivazione on-demand
 *   + Aggiunto LED3 su pin 6 come indicatore stato WiFi
 *   + WiFi si disabilita automaticamente dopo 5 minuti senza client connessi
 *   + Stati LED: OFF=WiFi spento, LAMPEGGIO=avvio, FISSO=attivo
 *   * CHANGED: WiFi non parte automaticamente all'avvio
 *   * CHANGED: Pressione pulsante durante WiFi attivo resetta timeout
 * v3.0.1 (2025-11-03) - Calibrazione PT100 e miglioramenti rete
 *   * FIXED: R_REF corretto a 172.0Ω (era 1000.0Ω) per lettura accurata PT100
 *   + Aggiunta stampa periodica temperature su seriale
 *   * CHANGED: IP Access Point fisso a 192.100.100.1
 *   * CHANGED: SSID Access Point: "EP401-FAN_CTRL"
 * v3.0.0 (2025-10-31) - WebServer e Controllo Automatico
 *   + Aggiunto WiFi Access Point e WebServer
 *   + Interfaccia web responsive per monitoraggio e controllo
 *   + Controllo automatico temperatura con isteresi 1°C
 *   + Modalità crepuscolare con riduzione configurabile (0-50%)
 *   + Sistema allarme pressostato con LED e output digitale
 *   + Salvataggio configurazioni in NVS (Preferences)
 *   + 3 output digitali (LED allarme + 2 liberi)
 *   + API REST JSON per integrazione
 * v2.0.0 (2025-10-30) - VERSIONE STABILE - Tachimetri con polling
 *   - Rimossi interrupt tachimetri (causa stack overflow)
 *   - Implementato polling a 10kHz per lettura tachimetri
 *   - Sistema 100% stabile senza crash
 * v1.7.0 (2025-10-30) - Tentativo debounce ISR
 * v1.6.1 (2025-10-30) - Tentativo stack aumentato
 * v1.6.0 (2025-10-30) - PWM 500Hz
 *******************************************************************************/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// ============================================================================
// CONFIGURAZIONE PIN
// ============================================================================

// PWM Output
#define PWM1_PIN 9
#define PWM2_PIN 10
#define PWM3_PIN 11

// Tachometer Input
#define TACH1_PIN 15
#define TACH2_PIN 16
#define TACH3_PIN 17
#define TACH4_PIN 18
#define TACH5_PIN 19
#define TACH6_PIN 20

// PT100 Input
#define PT100_1_PIN 4
#define PT100_2_PIN 5

// Digital Input
#define PRESSURE_PIN 36
#define TWILIGHT_PIN 35
#define WIFI_BUTTON_PIN 7

// Digital Output
#define LED_ALARM_PIN 3
#define OUTPUT_1_PIN 21
#define OUTPUT_2_PIN 47
#define OUTPUT_3_PIN 48
#define LED3_PIN 6

// ============================================================================
// CONFIGURAZIONE SISTEMA
// ============================================================================

// PWM
#define PWM_FREQ 500
#define PWM_MIN_DUTY 30
#define PWM_MAX_DUTY 100
#define TIMER_FREQ_HZ 50000

// Ventole
#define NUM_FANS 6
#define RPM_CALC_TIME 1000
#define PULSES_PER_REV 1

// Tachimetri - Polling
#define TACH_SAMPLE_US 100
#define TACH_DEBOUNCE_US 1000

// PT100
#define NUM_PT100 2
#define PT100_SAMPLES 10
#define PT100_READ_TIME 5000
#define PT100_PRINT_TIME 10000
#define ADC_RESOLUTION 4095.0
#define ADC_VREF 3.3
#define R_REF 172.0
#define PT100_R0 100.0
#define PT100_ALPHA 0.00385

// Controllo Temperatura
#define TEMP_HYSTERESIS 1.0
#define TEMP_MAX_DEFAULT 50.0
#define TEMP_MIN_DEFAULT 25.0

// Digital Input
#define DEBOUNCE_TIME 50

// WiFi
#define WIFI_SSID "EP401-FAN_CTRL"
#define WIFI_PASSWORD "ep401admin"
#define WIFI_CHANNEL 1
#define WIFI_TIMEOUT_MS 300000
#define CLIENT_CHECK_INTERVAL 10000
#define WIFI_LED_BLINK_TIME 500

// Network - IP Fisso
#define WIFI_IP_ADDR 192, 100, 100, 1
#define WIFI_GATEWAY 192, 100, 100, 1
#define WIFI_SUBNET 255, 255, 255, 0

// WebServer
#define WEB_UPDATE_TIME 2000

// ============================================================================
// ENUMERAZIONI
// ============================================================================

enum WiFiState {
  WIFI_STATE_OFF,
  WIFI_STATE_STARTING,
  WIFI_STATE_ACTIVE
};

// ============================================================================
// STRUTTURE DATI
// ============================================================================

struct PWMChannel {
  uint8_t pin;
  uint8_t dutyCycle;
  uint8_t targetDuty;
  uint8_t counter;
};

struct FanConfig {
  uint8_t id;
  uint8_t pwmChannelIndex;
  uint8_t tachPin;
  uint32_t pulseCount;
  bool lastState;
  unsigned long lastPulseTime;
  uint16_t rpm;
  bool connected;
};

struct PT100Sensor {
  uint8_t id;
  uint8_t pin;
  float samples[PT100_SAMPLES];
  uint8_t sampleIndex;
  float temperature;
  bool valid;
};

struct DigitalInput {
  uint8_t pin;
  bool state;
  bool lastState;
  unsigned long lastDebounce;
};

struct DigitalOutput {
  uint8_t pin;
  bool state;
};

struct AutoControlConfig {
  bool enabled;
  uint8_t tempSensor;
  float minTemp;
  float maxTemp;
  bool inHysteresis;
};

struct SystemConfig {
  bool twilightMode;
  uint8_t twilightReduction;
  bool pressureAlarmEnabled;
  AutoControlConfig pwm[3];
};

// ============================================================================
// VARIABILI GLOBALI
// ============================================================================

PWMChannel pwmChannels[3] = {
  { PWM1_PIN, PWM_MIN_DUTY, PWM_MIN_DUTY, 0 },
  { PWM2_PIN, PWM_MIN_DUTY, PWM_MIN_DUTY, 0 },
  { PWM3_PIN, PWM_MIN_DUTY, PWM_MIN_DUTY, 0 }
};

FanConfig fans[NUM_FANS] = {
  { 1, 0, TACH1_PIN, 0, HIGH, 0, 0, false },
  { 2, 0, TACH2_PIN, 0, HIGH, 0, 0, false },
  { 3, 1, TACH3_PIN, 0, HIGH, 0, 0, false },
  { 4, 1, TACH4_PIN, 0, HIGH, 0, 0, false },
  { 5, 2, TACH5_PIN, 0, HIGH, 0, 0, false },
  { 6, 2, TACH6_PIN, 0, HIGH, 0, 0, false }
};

PT100Sensor pt100Sensors[NUM_PT100] = {
  { 1, PT100_1_PIN, { 0 }, 0, 0.0, false },
  { 2, PT100_2_PIN, { 0 }, 0, 0.0, false }
};

DigitalInput pressureSwitch = { PRESSURE_PIN, false, false, 0 };
DigitalInput twilightSwitch = { TWILIGHT_PIN, false, false, 0 };
DigitalInput wifiButton = { WIFI_BUTTON_PIN, false, false, 0 };

DigitalOutput outputs[3] = {
  { LED_ALARM_PIN, false },
  { OUTPUT_1_PIN, false },
  { OUTPUT_2_PIN, false }
};

SystemConfig sysConfig = {
  false, 20, true, { { true, 1, TEMP_MIN_DEFAULT, TEMP_MAX_DEFAULT, false }, { true, 1, TEMP_MIN_DEFAULT, TEMP_MAX_DEFAULT, false }, { true, 2, TEMP_MIN_DEFAULT, TEMP_MAX_DEFAULT, false } }
};

hw_timer_t* pwmTimer = NULL;
WebServer* server = NULL;
Preferences preferences;

unsigned long lastRpmCalc = 0;
unsigned long lastPT100Read = 0;
unsigned long lastPT100Print = 0;
unsigned long lastTachSample = 0;
unsigned long lastConnectionCheck = 0;
bool pt100Initialized = false;
bool alarmActive = false;

// WiFi Management
WiFiState wifiState = WIFI_STATE_OFF;
unsigned long lastClientCheck = 0;
unsigned long lastClientActivity = 0;
unsigned long wifiLedLastToggle = 0;
bool wifiLedState = false;
bool wifiButtonPressed = false;

// ============================================================================
// ISR PWM
// ============================================================================

void IRAM_ATTR onPWMTimer() {
  uint8_t i;
  for (i = 0; i < 3; i++) {
    if (++pwmChannels[i].counter >= PWM_MAX_DUTY) {
      pwmChannels[i].counter = 0;
    }
    digitalWrite(pwmChannels[i].pin,
                 (pwmChannels[i].counter < (PWM_MAX_DUTY - pwmChannels[i].dutyCycle)) ? HIGH : LOW);
  }
}

// ============================================================================
// POLLING TACHIMETRI
// ============================================================================

void updateTachometers() {
  unsigned long now = micros();

  if (now - lastTachSample < TACH_SAMPLE_US) return;
  lastTachSample = now;

  for (uint8_t i = 0; i < NUM_FANS; i++) {
    bool currentState = digitalRead(fans[i].tachPin);

    if (fans[i].lastState == HIGH && currentState == LOW) {
      if (now - fans[i].lastPulseTime >= TACH_DEBOUNCE_US) {
        fans[i].pulseCount++;
        fans[i].lastPulseTime = now;
      }
    }

    fans[i].lastState = currentState;
  }
}

// ============================================================================
// INIZIALIZZAZIONE
// ============================================================================

void initPWM() {
  pinMode(PWM1_PIN, OUTPUT);
  pinMode(PWM2_PIN, OUTPUT);
  pinMode(PWM3_PIN, OUTPUT);

  digitalWrite(PWM1_PIN, HIGH);
  digitalWrite(PWM2_PIN, HIGH);
  digitalWrite(PWM3_PIN, HIGH);

  pwmTimer = timerBegin(TIMER_FREQ_HZ);
  if (pwmTimer == NULL) {
    Serial.println("ERRORE: Timer PWM!");
    return;
  }

  timerAttachInterrupt(pwmTimer, &onPWMTimer);
  timerAlarm(pwmTimer, 1, true, 0);
}

void initTachometers() {
  pinMode(TACH1_PIN, INPUT_PULLUP);
  pinMode(TACH2_PIN, INPUT_PULLUP);
  pinMode(TACH3_PIN, INPUT_PULLUP);
  pinMode(TACH4_PIN, INPUT_PULLUP);
  pinMode(TACH5_PIN, INPUT_PULLUP);
  pinMode(TACH6_PIN, INPUT_PULLUP);

  for (uint8_t i = 0; i < NUM_FANS; i++) {
    fans[i].lastState = digitalRead(fans[i].tachPin);
  }

  lastTachSample = micros();
}

void initPT100() {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  for (uint8_t i = 0; i < NUM_PT100; i++) {
    for (uint8_t j = 0; j < PT100_SAMPLES; j++) {
      pt100Sensors[i].samples[j] = 20.0;
    }
    pt100Sensors[i].temperature = 20.0;
    pt100Sensors[i].valid = false;
  }

  pt100Initialized = false;
}

void initDigitalInputs() {
  pinMode(PRESSURE_PIN, INPUT_PULLUP);
  pinMode(TWILIGHT_PIN, INPUT_PULLUP);

  // Pin con PULL-DOWN esterno - NO pull-up interno!
  pinMode(WIFI_BUTTON_PIN, INPUT);  // <-- Senza pull-up

  delay(10);

  pressureSwitch.state = digitalRead(PRESSURE_PIN);
  pressureSwitch.lastState = pressureSwitch.state;

  twilightSwitch.state = digitalRead(TWILIGHT_PIN);
  twilightSwitch.lastState = twilightSwitch.state;

  wifiButton.state = digitalRead(WIFI_BUTTON_PIN);
  wifiButton.lastState = wifiButton.state;

  // Debug
  Serial.print("WiFi Button stato iniziale (dovrebbe essere 0): ");
  Serial.println(wifiButton.state);
}

void initDigitalOutputs() {
  pinMode(LED_ALARM_PIN, OUTPUT);
  pinMode(OUTPUT_1_PIN, OUTPUT);
  pinMode(OUTPUT_2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);

  digitalWrite(LED_ALARM_PIN, LOW);
  digitalWrite(OUTPUT_1_PIN, LOW);
  digitalWrite(OUTPUT_2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);
}

// ============================================================================
// WIFI MANAGEMENT
// ============================================================================

void enableWiFi() {
  if (wifiState != WIFI_STATE_OFF) return;

  Serial.println("WiFi: Avvio...");
  wifiState = WIFI_STATE_STARTING;

  WiFi.mode(WIFI_AP);

  IPAddress local_IP(WIFI_IP_ADDR);
  IPAddress gateway(WIFI_GATEWAY);
  IPAddress subnet(WIFI_SUBNET);

  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("WiFi: ERRORE Config IP");
  }

  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);

  delay(100);

  if (server == NULL) {
    server = new WebServer(80);
    initWebServer();
  }

  wifiState = WIFI_STATE_ACTIVE;
  lastClientActivity = millis();
  lastClientCheck = millis();

  IPAddress IP = WiFi.softAPIP();
  Serial.print("WiFi: ATTIVO - IP: ");
  Serial.println(IP);
}

void disableWiFi() {
  if (wifiState == WIFI_STATE_OFF) return;

  Serial.println("WiFi: Spegnimento...");

  if (server != NULL) {
    server->stop();
  }

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  wifiState = WIFI_STATE_OFF;
  digitalWrite(LED3_PIN, LOW);

  Serial.println("WiFi: SPENTO");
}

void updateWiFiLED() {
  unsigned long now = millis();

  if (wifiState == WIFI_STATE_OFF) {
    digitalWrite(LED3_PIN, LOW);
  } else if (wifiState == WIFI_STATE_STARTING) {
    if (now - wifiLedLastToggle >= WIFI_LED_BLINK_TIME) {
      wifiLedState = !wifiLedState;
      digitalWrite(LED3_PIN, wifiLedState ? HIGH : LOW);
      wifiLedLastToggle = now;
    }
  } else if (wifiState == WIFI_STATE_ACTIVE) {
    digitalWrite(LED3_PIN, HIGH);
  }
}

void updateWiFiButton() {
  bool rawState = digitalRead(WIFI_BUTTON_PIN);

  updateDigitalInput(&wifiButton);

  // PULSANTE ATTIVO HIGH (con pull-down esterno)
  bool currentPressed = (wifiButton.state == HIGH);  // <-- HIGH = premuto

  static unsigned long lastDebugPrint = 0;
  if (millis() - lastDebugPrint > 1000) {
    Serial.print("BTN Raw:");
    Serial.print(rawState);
    Serial.print(" Debounced:");
    Serial.print(wifiButton.state);
    Serial.print(" Pressed:");
    Serial.print(currentPressed);
    Serial.print(" WiFi:");
    Serial.println(wifiState);
    lastDebugPrint = millis();
  }

  if (currentPressed && !wifiButtonPressed) {
    wifiButtonPressed = true;
    Serial.println("\n*** PULSANTE PREMUTO ***");

    if (wifiState == WIFI_STATE_OFF) {
      Serial.println("Attivazione WiFi...");
      enableWiFi();
    } else if (wifiState == WIFI_STATE_ACTIVE) {
      lastClientActivity = millis();
      Serial.println("WiFi: Timeout resettato");
    }
  } else if (!currentPressed && wifiButtonPressed) {
    wifiButtonPressed = false;
    Serial.println("*** PULSANTE RILASCIATO ***");
  }
}

void checkWiFiTimeout() {
  if (wifiState != WIFI_STATE_ACTIVE) return;

  unsigned long now = millis();

  if (now - lastClientCheck >= CLIENT_CHECK_INTERVAL) {
    lastClientCheck = now;

    uint8_t clientCount = WiFi.softAPgetStationNum();

    if (clientCount > 0) {
      lastClientActivity = now;
    }

    unsigned long timeSinceLastClient = now - lastClientActivity;

    if (timeSinceLastClient >= WIFI_TIMEOUT_MS) {
      Serial.println("WiFi: Timeout - Nessun client per 5 minuti");
      disableWiFi();
    }
  }
}

// ============================================================================
// PREFERENCES - SALVATAGGIO CONFIGURAZIONE
// ============================================================================

void loadConfiguration() {
  preferences.begin("ep401", false);

  sysConfig.twilightMode = preferences.getBool("twilight", false);
  sysConfig.twilightReduction = preferences.getUChar("twilRed", 20);
  sysConfig.pressureAlarmEnabled = preferences.getBool("pressEn", true);

  for (uint8_t i = 0; i < 3; i++) {
    char key[16];

    snprintf(key, 16, "pwm%d_en", i);
    sysConfig.pwm[i].enabled = preferences.getBool(key, true);

    snprintf(key, 16, "pwm%d_sens", i);
    sysConfig.pwm[i].tempSensor = preferences.getUChar(key, (i == 2) ? 2 : 1);

    snprintf(key, 16, "pwm%d_min", i);
    sysConfig.pwm[i].minTemp = preferences.getFloat(key, TEMP_MIN_DEFAULT);

    snprintf(key, 16, "pwm%d_max", i);
    sysConfig.pwm[i].maxTemp = preferences.getFloat(key, TEMP_MAX_DEFAULT);

    sysConfig.pwm[i].inHysteresis = false;
  }

  preferences.end();
}

void saveConfiguration() {
  preferences.begin("ep401", false);

  preferences.putBool("twilight", sysConfig.twilightMode);
  preferences.putUChar("twilRed", sysConfig.twilightReduction);
  preferences.putBool("pressEn", sysConfig.pressureAlarmEnabled);

  for (uint8_t i = 0; i < 3; i++) {
    char key[16];

    snprintf(key, 16, "pwm%d_en", i);
    preferences.putBool(key, sysConfig.pwm[i].enabled);

    snprintf(key, 16, "pwm%d_sens", i);
    preferences.putUChar(key, sysConfig.pwm[i].tempSensor);

    snprintf(key, 16, "pwm%d_min", i);
    preferences.putFloat(key, sysConfig.pwm[i].minTemp);

    snprintf(key, 16, "pwm%d_max", i);
    preferences.putFloat(key, sysConfig.pwm[i].maxTemp);
  }

  preferences.end();
}

// ============================================================================
// PT100
// ============================================================================

float readPT100Temperature(PT100Sensor* sensor) {
  uint32_t adcSum = 0;

  for (uint8_t i = 0; i < 10; i++) {
    adcSum += analogRead(sensor->pin);
    delay(1);
  }

  float adcValue = adcSum / 10.0;

  if (adcValue < 100 || adcValue > 4000) {
    sensor->valid = false;
    return 20.0;
  }

  float voltage = (adcValue / ADC_RESOLUTION) * ADC_VREF;

  if (voltage >= (ADC_VREF - 0.01)) voltage = ADC_VREF - 0.01;
  if (voltage <= 0.01) voltage = 0.01;

  float denominator = ADC_VREF - voltage;
  if (denominator < 0.01) denominator = 0.01;

  float resistance = (voltage * R_REF) / denominator;

  if (resistance < 50.0) resistance = 50.0;
  if (resistance > 200.0) resistance = 200.0;

  float temperature = (resistance - PT100_R0) / (PT100_R0 * PT100_ALPHA);

  sensor->samples[sensor->sampleIndex] = temperature;
  sensor->sampleIndex = (sensor->sampleIndex + 1) % PT100_SAMPLES;

  float sum = 0.0;
  for (uint8_t i = 0; i < PT100_SAMPLES; i++) {
    sum += sensor->samples[i];
  }
  float avgTemp = sum / PT100_SAMPLES;

  sensor->valid = (avgTemp >= 0.0 && avgTemp <= 70.0);

  return avgTemp;
}

void updatePT100() {
  unsigned long now = millis();

  if (!pt100Initialized) {
    if (now > 3000) {
      pt100Initialized = true;
      lastPT100Read = now;
    }
    return;
  }

  if (now - lastPT100Read >= PT100_READ_TIME) {
    for (uint8_t i = 0; i < NUM_PT100; i++) {
      pt100Sensors[i].temperature = readPT100Temperature(&pt100Sensors[i]);
    }
    lastPT100Read = now;
  }
}

void printTemperatures() {
  unsigned long now = millis();

  if (now - lastPT100Print >= PT100_PRINT_TIME) {
    Serial.println("\n=== TEMPERATURE ===");
    for (uint8_t i = 0; i < NUM_PT100; i++) {
      Serial.print("PT10");
      Serial.print(pt100Sensors[i].id);
      Serial.print(": ");
      if (pt100Sensors[i].valid) {
        Serial.print(pt100Sensors[i].temperature, 1);
        Serial.println("°C");
      } else {
        Serial.println("ERRORE");
      }
    }
    Serial.println("==================\n");
    lastPT100Print = now;
  }
}

// ============================================================================
// INGRESSI DIGITALI
// ============================================================================

void updateDigitalInput(DigitalInput* input) {
  bool reading = digitalRead(input->pin);

  if (reading != input->lastState) {
    input->lastDebounce = millis();
  }

  if ((millis() - input->lastDebounce) > DEBOUNCE_TIME) {
    if (reading != input->state) {
      input->state = reading;
    }
  }

  input->lastState = reading;
}

void updateDigitalInputs() {
  updateDigitalInput(&pressureSwitch);
  updateDigitalInput(&twilightSwitch);
}

// ============================================================================
// USCITE DIGITALI E ALLARME
// ============================================================================

void setDigitalOutput(uint8_t index, bool state) {
  if (index >= 3) return;
  outputs[index].state = state;
  digitalWrite(outputs[index].pin, state ? HIGH : LOW);
}

void updateAlarm() {
  bool alarm = sysConfig.pressureAlarmEnabled && pressureSwitch.state;

  if (alarm != alarmActive) {
    alarmActive = alarm;
    setDigitalOutput(0, alarm);
    setDigitalOutput(1, alarm);
  }
}

// ============================================================================
// CONTROLLO PWM
// ============================================================================

void setPWM(uint8_t channelIndex, uint8_t duty) {
  if (channelIndex >= 3) return;

  if (duty < PWM_MIN_DUTY) duty = PWM_MIN_DUTY;
  if (duty > PWM_MAX_DUTY) duty = PWM_MAX_DUTY;

  pwmChannels[channelIndex].targetDuty = duty;
  pwmChannels[channelIndex].dutyCycle = duty;
}

uint8_t calculatePWMFromTemp(uint8_t channelIndex) {
  if (channelIndex >= 3) return PWM_MIN_DUTY;

  AutoControlConfig* cfg = &sysConfig.pwm[channelIndex];

  if (!cfg->enabled || cfg->tempSensor < 1 || cfg->tempSensor > 2) {
    return PWM_MIN_DUTY;
  }

  PT100Sensor* sensor = &pt100Sensors[cfg->tempSensor - 1];

  if (!sensor->valid) return PWM_MIN_DUTY;

  float temp = sensor->temperature;
  float minTemp = cfg->minTemp;
  float maxTemp = cfg->maxTemp;

  if (cfg->inHysteresis) {
    if (temp < (minTemp - TEMP_HYSTERESIS)) {
      cfg->inHysteresis = false;
      return PWM_MIN_DUTY;
    }
  } else {
    if (temp < minTemp) {
      return PWM_MIN_DUTY;
    } else {
      cfg->inHysteresis = true;
    }
  }

  if (temp >= maxTemp) {
    return PWM_MAX_DUTY;
  }

  float range = maxTemp - minTemp;
  if (range <= 0) return PWM_MIN_DUTY;

  float tempRatio = (temp - minTemp) / range;
  uint8_t pwm = PWM_MIN_DUTY + (uint8_t)(tempRatio * (PWM_MAX_DUTY - PWM_MIN_DUTY));

  return constrain(pwm, PWM_MIN_DUTY, PWM_MAX_DUTY);
}

void applyTwilightMode(uint8_t* duty) {
  if (!sysConfig.twilightMode) return;

  uint8_t originalDuty = *duty;
  uint8_t reduction = (*duty * sysConfig.twilightReduction) / 100;

  if (*duty > reduction) {
    *duty = *duty - reduction;
  } else {
    *duty = PWM_MIN_DUTY;
  }

  if (*duty < PWM_MIN_DUTY) {
    *duty = PWM_MIN_DUTY;
  }
}

void updateAutomaticControl() {
  if (!pt100Initialized) return;

  for (uint8_t i = 0; i < 3; i++) {
    uint8_t dutyFromTemp = calculatePWMFromTemp(i);
    applyTwilightMode(&dutyFromTemp);
    setPWM(i, dutyFromTemp);
  }
}

// ============================================================================
// CALCOLO RPM E CONNESSIONE VENTOLE
// ============================================================================

void calculateRPM() {
  unsigned long now = millis();
  unsigned long deltaTime = now - lastRpmCalc;

  if (deltaTime >= RPM_CALC_TIME) {
    float minutes = deltaTime / 60000.0;

    for (uint8_t i = 0; i < NUM_FANS; i++) {
      uint32_t pulses = fans[i].pulseCount;
      fans[i].pulseCount = 0;

      fans[i].rpm = (uint16_t)((pulses / PULSES_PER_REV) / minutes);
      fans[i].connected = (fans[i].rpm > 100);
    }

    lastRpmCalc = now;
  }
}

// ============================================================================
// WEBSERVER - HTML
// ============================================================================

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EP401 - Controllo Ventole</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;padding:20px}
.header{background:linear-gradient(135deg,#16213e,#0f3460);padding:20px;border-radius:10px;margin-bottom:20px;text-align:center;position:relative}
.header h1{font-size:28px;margin-bottom:5px}
.header .version{font-size:14px;color:#aaa}
.save-indicator{position:absolute;top:20px;right:20px;padding:8px 16px;border-radius:6px;font-size:12px;font-weight:bold;opacity:0;transition:opacity 0.3s}
.save-indicator.saving{background:#ff9800;color:#fff;opacity:1}
.save-indicator.saved{background:#4caf50;color:#fff;opacity:1}
.alarm{background:#d32f2f;color:#fff;padding:15px;border-radius:8px;margin-bottom:20px;text-align:center;font-weight:bold;display:none}
.alarm.active{display:block;animation:pulse 1.5s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:0.7}}
.container{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:20px}
.card{background:#16213e;border-radius:10px;padding:20px;box-shadow:0 4px 6px rgba(0,0,0,0.3)}
.card h2{font-size:18px;margin-bottom:15px;color:#4a9eff;border-bottom:2px solid #0f3460;padding-bottom:8px}
.fan-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px}
.fan-item{background:#0f3460;padding:12px;border-radius:6px;text-align:center}
.fan-item.offline{opacity:0.5;background:#2a2a3e}
.fan-rpm{font-size:24px;font-weight:bold;color:#4a9eff}
.temp-item{background:#0f3460;padding:15px;border-radius:6px;margin-bottom:10px}
.temp-value{font-size:32px;font-weight:bold;color:#ff6b6b}
.input-item{background:#0f3460;padding:12px;border-radius:6px;margin-bottom:8px;display:flex;justify-content:space-between;align-items:center}
.status{display:inline-block;padding:4px 12px;border-radius:4px;font-size:12px;font-weight:bold}
.status.high{background:#4caf50;color:#fff}
.status.low{background:#666;color:#fff}
.config-section{margin-top:15px;padding-top:15px;border-top:1px solid #0f3460}
.toggle{position:relative;display:inline-block;width:50px;height:24px}
.toggle input{opacity:0;width:0;height:0}
.slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:#666;border-radius:24px;transition:.3s}
.slider:before{position:absolute;content:"";height:18px;width:18px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.3s}
input:checked+.slider{background:#4caf50}
input:checked+.slider:before{transform:translateX(26px)}
.form-group{margin-bottom:15px}
.form-group label{display:block;margin-bottom:5px;font-size:14px;color:#aaa}
.form-group select,.form-group input{width:100%;padding:8px;border-radius:4px;border:1px solid #0f3460;background:#0f3460;color:#fff;font-size:14px}
.pwm-config{background:#1a2332;padding:12px;border-radius:6px;margin-bottom:10px}
.slider-container{margin:10px 0}
.slider-container input[type=range]{width:100%;height:6px;border-radius:3px;background:#0f3460;outline:none}
.slider-value{text-align:center;font-size:18px;color:#4a9eff;margin-top:5px}
.auto-save-info{text-align:center;font-size:12px;color:#aaa;margin-top:15px;padding:10px;background:#0f3460;border-radius:6px}
</style>
</head>
<body>
<div class="header">
<h1>🌀 EP401 - Sistema Ventole</h1>
<div class="version">v3.0.2 | ESP32-S3</div>
<div id="saveIndicator" class="save-indicator">💾 Salvato</div>
</div>
<div id="alarm" class="alarm">⚠️ ALLARME PRESSOSTATO ATTIVO</div>
<div class="container">
<div class="card">
<h2>🌀 Ventole</h2>
<div class="fan-grid" id="fanGrid"></div>
</div>
<div class="card">
<h2>🌡️ Temperature</h2>
<div id="tempGrid"></div>
</div>
<div class="card">
<h2>📡 Ingressi Digitali</h2>
<div id="inputsGrid"></div>
</div>
<div class="card">
<h2>⚙️ Configurazione</h2>
<div class="form-group">
<label>🌙 Modalità Crepuscolare</label>
<label class="toggle">
<input type="checkbox" id="twilightMode" onchange="scheduleAutoSave()">
<span class="slider"></span>
</label>
</div>
<div class="slider-container">
<label>Riduzione Velocità: <span id="twilightValue">20</span>%</label>
<input type="range" id="twilightReduction" min="0" max="50" value="20" 
oninput="document.getElementById('twilightValue').innerText=this.value;scheduleAutoSave()">
</div>
<div class="config-section">
<div class="form-group">
<label>🚨 Lettura Pressostato</label>
<label class="toggle">
<input type="checkbox" id="pressureEnabled" checked onchange="scheduleAutoSave()">
<span class="slider"></span>
</label>
</div>
</div>
<div class="config-section">
<h3 style="margin-bottom:10px;color:#4a9eff">PWM / Temperatura</h3>
<div id="pwmConfigs"></div>
</div>
<div class="auto-save-info">
✨ Le modifiche vengono salvate automaticamente
</div>
</div>
</div>
<script>
let config={};
let pwmConfigsGenerated=false;
let saveTimeout=null;
let isSaving=false;

function showSaveIndicator(status){
let indicator=document.getElementById('saveIndicator');
indicator.className='save-indicator '+status;
if(status==='saved'){
setTimeout(()=>{
indicator.className='save-indicator';
},2000);
}
}

function scheduleAutoSave(){
if(isSaving) return;
showSaveIndicator('saving');
clearTimeout(saveTimeout);
saveTimeout=setTimeout(()=>{
autoSaveConfig();
},1000);
}

function autoSaveConfig(){
if(isSaving) return;
isSaving=true;

config.twilightMode=document.getElementById('twilightMode').checked;
config.twilightReduction=parseInt(document.getElementById('twilightReduction').value);
config.pressureEnabled=document.getElementById('pressureEnabled').checked;

config.pwm=[];
for(let i=0;i<3;i++){
config.pwm.push({
sensor:parseInt(document.getElementById('pwm'+i+'Sensor').value),
minTemp:parseFloat(document.getElementById('pwm'+i+'Min').value),
maxTemp:parseFloat(document.getElementById('pwm'+i+'Max').value)
});
}

fetch('/api/config',{
method:'POST',
headers:{'Content-Type':'application/json'},
body:JSON.stringify(config)
}).then(r=>r.json()).then(data=>{
isSaving=false;
if(data.success){
showSaveIndicator('saved');
}else{
showSaveIndicator('');
console.error('Errore salvataggio');
}
}).catch(e=>{
isSaving=false;
showSaveIndicator('');
console.error('Errore:',e);
});
}

function updateStatus(){
fetch('/api/status').then(r=>r.json()).then(data=>{
document.getElementById('alarm').className='alarm'+(data.alarm?' active':'');

let fh='';
data.fans.forEach(f=>{
fh+=`<div class="fan-item ${f.connected?'':'offline'}">
<div style="font-weight:bold">V${f.id}</div>
<div class="fan-rpm">${f.rpm}</div>
<div style="font-size:11px;color:#aaa">${f.connected?'ONLINE':'OFFLINE'}</div>
</div>`;
});
document.getElementById('fanGrid').innerHTML=fh;

let th='';
data.temps.forEach(t=>{
th+=`<div class="temp-item">
<div style="font-size:14px;color:#aaa">PT10${t.id}</div>
<div class="temp-value">${t.temp.toFixed(1)}°C</div>
<div style="font-size:12px;color:#aaa">${t.valid?'OK':'ERRORE'}</div>
</div>`;
});
document.getElementById('tempGrid').innerHTML=th;

let ih=`<div class="input-item">
<span>Pressostato</span>
<span class="status ${data.inputs.pressure?'high':'low'}">${data.inputs.pressure?'HIGH':'LOW'}</span>
</div>
<div class="input-item">
<span>Crepuscolare</span>
<span class="status ${data.inputs.twilight?'high':'low'}">${data.inputs.twilight?'HIGH':'LOW'}</span>
</div>`;
document.getElementById('inputsGrid').innerHTML=ih;

config=data.config;

if(document.activeElement.id!=='twilightMode'){
document.getElementById('twilightMode').checked=config.twilightMode;
}
if(document.activeElement.id!=='twilightReduction'){
document.getElementById('twilightReduction').value=config.twilightReduction;
document.getElementById('twilightValue').innerText=config.twilightReduction;
}
if(document.activeElement.id!=='pressureEnabled'){
document.getElementById('pressureEnabled').checked=config.pressureEnabled;
}

if(!pwmConfigsGenerated){
let ph='';
for(let i=0;i<3;i++){
let p=config.pwm[i];
ph+=`<div class="pwm-config">
<h4 style="margin-bottom:8px">PWM${i+1} (V${i*2+1}+V${i*2+2})</h4>
<div class="form-group">
<label>Sensore Temperatura</label>
<select id="pwm${i}Sensor" onchange="scheduleAutoSave()">
<option value="1">PT101</option>
<option value="2">PT102</option>
</select>
</div>
<div class="form-group">
<label>Soglia Minima (°C)</label>
<input type="number" id="pwm${i}Min" step="0.5" min="0" max="60" onchange="scheduleAutoSave()">
</div>
<div class="form-group">
<label>Soglia Massima (°C)</label>
<input type="number" id="pwm${i}Max" step="0.5" min="0" max="80" onchange="scheduleAutoSave()">
</div>
</div>`;
}
document.getElementById('pwmConfigs').innerHTML=ph;
pwmConfigsGenerated=true;
}

for(let i=0;i<3;i++){
let p=config.pwm[i];
if(document.activeElement.id!=='pwm'+i+'Sensor'){
document.getElementById('pwm'+i+'Sensor').value=p.sensor;
}
if(document.activeElement.id!=='pwm'+i+'Min'){
document.getElementById('pwm'+i+'Min').value=p.minTemp;
}
if(document.activeElement.id!=='pwm'+i+'Max'){
document.getElementById('pwm'+i+'Max').value=p.maxTemp;
}
}

}).catch(e=>console.error('Error:',e));
}

setInterval(updateStatus,2000);
updateStatus();
</script>
</body>
</html>
)rawliteral";

// ============================================================================
// WEBSERVER - API
// ============================================================================

void handleRoot() {
  server->send(200, "text/html", HTML_PAGE);
}

void handleStatus() {
  String json = "{";

  json += "\"alarm\":" + String(alarmActive ? "true" : "false") + ",";

  json += "\"fans\":[";
  for (uint8_t i = 0; i < NUM_FANS; i++) {
    if (i > 0) json += ",";
    json += "{\"id\":" + String(fans[i].id) + ",";
    json += "\"rpm\":" + String(fans[i].rpm) + ",";
    json += "\"connected\":" + String(fans[i].connected ? "true" : "false") + "}";
  }
  json += "],";

  json += "\"temps\":[";
  for (uint8_t i = 0; i < NUM_PT100; i++) {
    if (i > 0) json += ",";
    json += "{\"id\":" + String(pt100Sensors[i].id) + ",";
    json += "\"temp\":" + String(pt100Sensors[i].temperature, 1) + ",";
    json += "\"valid\":" + String(pt100Sensors[i].valid ? "true" : "false") + "}";
  }
  json += "],";

  json += "\"inputs\":{";
  json += "\"pressure\":" + String(pressureSwitch.state ? "true" : "false") + ",";
  json += "\"twilight\":" + String(twilightSwitch.state ? "true" : "false");
  json += "},";

  json += "\"config\":{";
  json += "\"twilightMode\":" + String(sysConfig.twilightMode ? "true" : "false") + ",";
  json += "\"twilightReduction\":" + String(sysConfig.twilightReduction) + ",";
  json += "\"pressureEnabled\":" + String(sysConfig.pressureAlarmEnabled ? "true" : "false") + ",";
  json += "\"pwm\":[";
  for (uint8_t i = 0; i < 3; i++) {
    if (i > 0) json += ",";
    json += "{\"sensor\":" + String(sysConfig.pwm[i].tempSensor) + ",";
    json += "\"minTemp\":" + String(sysConfig.pwm[i].minTemp, 1) + ",";
    json += "\"maxTemp\":" + String(sysConfig.pwm[i].maxTemp, 1) + "}";
  }
  json += "]";
  json += "}";

  json += "}";

  server->send(200, "application/json", json);
}

void handleConfig() {
  if (server->method() != HTTP_POST) {
    server->send(405, "application/json", "{\"success\":false}");
    return;
  }

  String body = server->arg("plain");

  int twilightIdx = body.indexOf("\"twilightMode\"");
  if (twilightIdx >= 0) {
    String after = body.substring(twilightIdx);
    int colonIdx = after.indexOf(":");
    if (colonIdx >= 0) {
      String valueSection = after.substring(colonIdx + 1, colonIdx + 10);
      valueSection.trim();
      valueSection.toLowerCase();
      sysConfig.twilightMode = (valueSection.indexOf("true") >= 0);
    }
  }

  int reductionIdx = body.indexOf("\"twilightReduction\"");
  if (reductionIdx >= 0) {
    String after = body.substring(reductionIdx);
    int colonIdx = after.indexOf(":");
    if (colonIdx >= 0) {
      int commaIdx = after.indexOf(",", colonIdx);
      int braceIdx = after.indexOf("}", colonIdx);
      int endIdx = (commaIdx > 0 && commaIdx < braceIdx) ? commaIdx : braceIdx;
      String valueStr = after.substring(colonIdx + 1, endIdx);
      valueStr.trim();
      sysConfig.twilightReduction = constrain(valueStr.toInt(), 0, 50);
    }
  }

  int pressureIdx = body.indexOf("\"pressureEnabled\"");
  if (pressureIdx >= 0) {
    String after = body.substring(pressureIdx);
    int colonIdx = after.indexOf(":");
    if (colonIdx >= 0) {
      String valueSection = after.substring(colonIdx + 1, colonIdx + 10);
      valueSection.trim();
      valueSection.toLowerCase();
      sysConfig.pressureAlarmEnabled = (valueSection.indexOf("true") >= 0);
    }
  }

  int pwmArrayIdx = body.indexOf("\"pwm\"");
  if (pwmArrayIdx >= 0) {
    String pwmSection = body.substring(pwmArrayIdx);
    int arrayStart = pwmSection.indexOf("[");
    int arrayEnd = pwmSection.indexOf("]");

    if (arrayStart >= 0 && arrayEnd > arrayStart) {
      String arrayContent = pwmSection.substring(arrayStart + 1, arrayEnd);

      int objCount = 0;
      int searchFrom = 0;

      while (objCount < 3) {
        int objStart = arrayContent.indexOf("{", searchFrom);
        int objEnd = arrayContent.indexOf("}", objStart);

        if (objStart < 0 || objEnd < 0) break;

        String obj = arrayContent.substring(objStart, objEnd + 1);

        int sensorIdx = obj.indexOf("\"sensor\"");
        if (sensorIdx >= 0) {
          String after = obj.substring(sensorIdx);
          int colonIdx = after.indexOf(":");
          int commaIdx = after.indexOf(",", colonIdx);
          String val = after.substring(colonIdx + 1, commaIdx);
          val.trim();
          sysConfig.pwm[objCount].tempSensor = constrain(val.toInt(), 1, 2);
        }

        int minIdx = obj.indexOf("\"minTemp\"");
        if (minIdx >= 0) {
          String after = obj.substring(minIdx);
          int colonIdx = after.indexOf(":");
          int commaIdx = after.indexOf(",", colonIdx);
          if (commaIdx < 0) commaIdx = after.indexOf("}", colonIdx);
          String val = after.substring(colonIdx + 1, commaIdx);
          val.trim();
          sysConfig.pwm[objCount].minTemp = constrain(val.toFloat(), 0.0, 60.0);
        }

        int maxIdx = obj.indexOf("\"maxTemp\"");
        if (maxIdx >= 0) {
          String after = obj.substring(maxIdx);
          int colonIdx = after.indexOf(":");
          int braceIdx = after.indexOf("}", colonIdx);
          String val = after.substring(colonIdx + 1, braceIdx);
          val.trim();
          sysConfig.pwm[objCount].maxTemp = constrain(val.toFloat(), 0.0, 80.0);
        }

        objCount++;
        searchFrom = objEnd + 1;
      }
    }
  }

  saveConfiguration();

  server->send(200, "application/json", "{\"success\":true}");
}

void initWebServer() {
  server->on("/", handleRoot);
  server->on("/api/status", handleStatus);
  server->on("/api/config", handleConfig);

  server->begin();
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n========================================");
  Serial.println("  EP401v1 - Automazione Ventole");
  Serial.println("  Versione: 3.0.2");
  Serial.println("========================================");

  Serial.print("PWM...");
  initPWM();
  Serial.println("OK");

  Serial.print("Tachimetri...");
  initTachometers();
  Serial.println("OK");

  Serial.print("PT100...");
  initPT100();
  Serial.println("OK");

  Serial.print("Input...");
  initDigitalInputs();
  Serial.println("OK");

  Serial.print("Output...");
  initDigitalOutputs();
  Serial.println("OK");

  Serial.print("Config...");
  loadConfiguration();
  Serial.println("OK");

  Serial.println("\nSistema pronto!");
  Serial.println("WiFi: SPENTO (premi pulsante per attivare)");

  lastRpmCalc = millis();
}

// ============================================================================
// LOOP
// ============================================================================

void loop() {
  updateTachometers();
  calculateRPM();
  updatePT100();
  printTemperatures();
  updateDigitalInputs();
  updateAlarm();
  updateAutomaticControl();

  updateWiFiButton();
  updateWiFiLED();
  checkWiFiTimeout();

  if (wifiState == WIFI_STATE_ACTIVE && server != NULL) {
    server->handleClient();
  }
}