// ============================================
// TEST DISPLAY - CONFIGURAZIONE CORRETTA PER ESP32-S3-Touch-LCD-5
// ============================================
// Pin mapping corretto + IO expander CH422G per backlight

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>

// ============================================
// CONFIGURAZIONE DISPLAY RGB CORRETTA
// ============================================
#define TFT_WIDTH 800
#define TFT_HEIGHT 480

// Pin RGB corretti per ESP32-S3-Touch-LCD-5 Waveshare (dal datasheet ufficiale)
#define TFT_DE 5
#define TFT_VSYNC 3
#define TFT_HSYNC 46
#define TFT_PCLK 7

// RGB Data pins (verificati da documentazione Waveshare)
#define TFT_R0 1
#define TFT_R1 2
#define TFT_R2 42
#define TFT_R3 41
#define TFT_R4 40

#define TFT_G0 39
#define TFT_G1 0
#define TFT_G2 45
#define TFT_G3 48
#define TFT_G4 47
#define TFT_G5 21

#define TFT_B0 14
#define TFT_B1 38
#define TFT_B2 18
#define TFT_B3 17
#define TFT_B4 10

// Timing parameters corretti (dal esempio funzionante GitHub)
#define HSYNC_POLARITY 0
#define HSYNC_FRONT_PORCH 40
#define HSYNC_PULSE_WIDTH 48
#define HSYNC_BACK_PORCH 88

#define VSYNC_POLARITY 0
#define VSYNC_FRONT_PORCH 13
#define VSYNC_PULSE_WIDTH 3
#define VSYNC_BACK_PORCH 32

#define PCLK_ACTIVE_NEG 1
#define RGB_FREQ 16000000  // 16MHz come nell'esempio funzionante

// ============================================
// IO EXPANDER CH422G CONFIGURATION
// ============================================
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9
#define CH422G_ADDRESS 0x24  // Indirizzo I2C del CH422G

// CH422G pin mapping (dal datasheet)
#define CH422G_TP_RST 0   // Touch reset
#define CH422G_LCD_RST 1  // LCD reset
#define CH422G_LCD_BL 2   // LCD backlight
#define CH422G_SD_CS 3    // SD card CS

// ============================================
// VARIABILI GLOBALI
// ============================================
Arduino_ESP32RGBPanel *bus = nullptr;
Arduino_RGB_Display *gfx = nullptr;

// ============================================
// IO EXPANDER FUNCTIONS
// ============================================

bool initIOExpander() {
  Serial.println("=== INIT IO EXPANDER CH422G ===");

  // Inizializza I2C
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);  // 400kHz

  delay(100);

  // Test comunicazione con CH422G
  Wire.beginTransmission(CH422G_ADDRESS);
  if (Wire.endTransmission() != 0) {
    Serial.println("❌ CH422G non trovato su I2C");
    return false;
  }

  Serial.println("✅ CH422G trovato");

  // Configura tutti i pin come output
  Wire.beginTransmission(CH422G_ADDRESS);
  Wire.write(0x01);  // Set direction register
  Wire.write(0x00);  // All pins as output
  Wire.endTransmission();

  delay(10);

  // Imposta stati iniziali:
  // - LCD_RST = HIGH (rilascia reset LCD)
  // - TP_RST = HIGH (rilascia reset touch)
  // - LCD_BL = HIGH (accende backlight)
  // - SD_CS = HIGH (disabilita SD)

  uint8_t pin_states = (1 << CH422G_LCD_RST) | (1 << CH422G_TP_RST) | (1 << CH422G_LCD_BL) | (1 << CH422G_SD_CS);

  Wire.beginTransmission(CH422G_ADDRESS);
  Wire.write(0x00);  // Output register
  Wire.write(pin_states);
  Wire.endTransmission();

  delay(100);  // Aspetta che il display si stabilizzi

  Serial.println("✅ IO Expander configurato - Backlight ON");
  return true;
}

// ============================================
// DISPLAY FUNCTIONS
// ============================================

bool initDisplay() {
  Serial.println("=== INIT DISPLAY RGB CON PIN CORRETTI ===");

  // Creazione bus RGB con parametri corretti
  bus = new Arduino_ESP32RGBPanel(
    TFT_DE, TFT_VSYNC, TFT_HSYNC, TFT_PCLK,
    TFT_R0, TFT_R1, TFT_R2, TFT_R3, TFT_R4,
    TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
    TFT_B0, TFT_B1, TFT_B2, TFT_B3, TFT_B4,
    HSYNC_POLARITY, HSYNC_FRONT_PORCH, HSYNC_PULSE_WIDTH, HSYNC_BACK_PORCH,
    VSYNC_POLARITY, VSYNC_FRONT_PORCH, VSYNC_PULSE_WIDTH, VSYNC_BACK_PORCH,
    PCLK_ACTIVE_NEG, RGB_FREQ, true /* auto_flush */
  );

  if (!bus) {
    Serial.println("❌ Errore creazione RGB bus");
    return false;
  }

  gfx = new Arduino_RGB_Display(TFT_WIDTH, TFT_HEIGHT, bus);

  if (!gfx) {
    Serial.println("❌ Errore creazione display");
    return false;
  }

  if (!gfx->begin()) {
    Serial.println("❌ Errore inizializzazione display");
    return false;
  }

  Serial.printf("✅ Display RGB OK: %dx%d @ %dMHz\n",
                TFT_WIDTH, TFT_HEIGHT, RGB_FREQ / 1000000);
  return true;
}

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("=== TEST ESP32-S3-Touch-LCD-5 - PIN MAPPING CORRETTO ===");
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());

  // STEP 1: Inizializza IO expander per backlight
  if (!initIOExpander()) {
    Serial.println("❌ ERRORE: IO Expander non funziona");
    Serial.println("🔧 Possibili cause:");
    Serial.println("   - Pin I2C errati");
    Serial.println("   - CH422G non presente");
    Serial.println("   - Board diversa da ESP32-S3-Touch-LCD-5");
    while (1) {
      delay(1000);
      Serial.println("STUCK - Verifica hardware");
    }
  }

  // STEP 2: Inizializza display RGB
  if (!initDisplay()) {
    Serial.println("❌ ERRORE: Display RGB non funziona");
    while (1) {
      delay(1000);
      Serial.println("STUCK - Display non inizializza");
    }
  }

  Serial.println("🎉 SISTEMA COMPLETO INIZIALIZZATO!");

  // STEP 3: Test visivo completo
  Serial.println("=== TEST VISIVO COMPLETO ===");

  // Test 1: Colori puri con delay maggiore per osservazione
  Serial.println("Test 1: Colori puri...");
  gfx->fillScreen(BLACK);
  delay(2000);
  Serial.println("  - Nero (2 sec)");

  gfx->fillScreen(RED);
  delay(2000);
  Serial.println("  - Rosso (2 sec)");

  gfx->fillScreen(GREEN);
  delay(2000);
  Serial.println("  - Verde (2 sec)");

  gfx->fillScreen(BLUE);
  delay(2000);
  Serial.println("  - Blu (2 sec)");

  gfx->fillScreen(WHITE);
  delay(2000);
  Serial.println("  - Bianco (2 sec)");

  // Test 2: Pattern geometrico
  Serial.println("Test 2: Pattern geometrico...");
  gfx->fillScreen(BLACK);

  // Griglia colorata
  for (int x = 0; x < 800; x += 100) {
    for (int y = 0; y < 480; y += 100) {
      uint16_t color = (x / 100 + y / 100) % 2 ? WHITE : RED;
      gfx->fillRect(x, y, 100, 100, color);
    }
  }

  delay(3000);
  Serial.println("  - Pattern scacchiera (3 sec)");

  // Test 3: Cerchi colorati
  gfx->fillScreen(BLACK);
  gfx->fillCircle(200, 240, 100, RED);
  gfx->fillCircle(400, 240, 100, GREEN);
  gfx->fillCircle(600, 240, 100, BLUE);

  delay(3000);
  Serial.println("  - Cerchi RGB (3 sec)");

  // Test 4: Testo
  gfx->fillScreen(BLACK);
  gfx->setCursor(50, 100);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(3);
  gfx->println("ESP32-S3-Touch-LCD-5");
  gfx->setCursor(50, 200);
  gfx->setTextColor(GREEN);
  gfx->setTextSize(2);
  gfx->println("DISPLAY FUNZIONA!");
  gfx->setCursor(50, 300);
  gfx->setTextColor(YELLOW);
  gfx->println("Pin mapping corretto");
  gfx->setCursor(50, 350);
  gfx->setTextColor(CYAN);
  gfx->println("Backlight attivo");

  Serial.println("  - Testo informativo");

  Serial.println("🎉 TEST COMPLETATO!");
  Serial.println("💡 Se vedi tutti i test sul display, tutto funziona!");
  Serial.printf("Free Heap finale: %d bytes\n", ESP.getFreeHeap());
}

// ============================================
// LOOP
// ============================================
void loop() {
  static uint32_t last_update = 0;
  static uint32_t counter = 0;

  // Aggiorna ogni 5 secondi
  if (millis() - last_update > 5000) {
    counter++;

    // Alternate tra info display e colore casuale
    if (counter % 2 == 1) {
      // Info display
      gfx->fillScreen(BLACK);
      gfx->setCursor(50, 100);
      gfx->setTextColor(WHITE);
      gfx->setTextSize(3);
      gfx->printf("UPTIME: %lu sec", millis() / 1000);
      gfx->setCursor(50, 200);
      gfx->setTextColor(GREEN);
      gfx->setTextSize(2);
      gfx->printf("Heap: %d bytes", ESP.getFreeHeap());
      gfx->setCursor(50, 300);
      gfx->setTextColor(YELLOW);
      gfx->printf("Loop: %lu", counter);
    } else {
      // Colore casuale
      uint16_t colors[] = { RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA };
      uint16_t color = colors[counter % 6];
      gfx->fillScreen(color);

      gfx->setCursor(200, 200);
      gfx->setTextColor(WHITE);
      gfx->setTextSize(4);
      gfx->println("COLOR TEST");
    }

    Serial.printf("⏰ Loop %lu - Heap: %d\n", counter, ESP.getFreeHeap());
    last_update = millis();
  }

  delay(10);
}

/*
📋 RISULTATI ATTESI:

🟢 SE TUTTO FUNZIONA:
   === TEST ESP32-S3-Touch-LCD-5 - PIN MAPPING CORRETTO ===
   ✅ CH422G trovato
   ✅ IO Expander configurato - Backlight ON
   ✅ Display RGB OK: 800x480 @ 16MHz
   🎉 SISTEMA COMPLETO INIZIALIZZATO!
   
   SUL DISPLAY:
   - Sequenza colori puri (nero→rosso→verde→blu→bianco)
   - Pattern scacchiera rosso/bianco
   - Tre cerchi colorati RGB
   - Testo informativo
   - Loop alternato info/colori

🔴 SE CH422G FALLISCE:
   → Board diversa o pin I2C errati
   → Verifica che sia ESP32-S3-Touch-LCD-5

🟡 SE DISPLAY ANCORA BIANCO:
   → Pin mapping ancora errato
   → Frequenza RGB problematica

🎯 QUESTO TEST È DEFINITIVO:
   Se funziona, abbiamo risolto tutto!
   Se non funziona, dobbiamo verificare il modello esatto della board.
*/