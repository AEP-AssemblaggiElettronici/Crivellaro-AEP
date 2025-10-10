#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include <time.h>
#include <sys/time.h>
#include <esp_task_wdt.h>
#include <functional>
#include "SPIFFS.h"

#include <SD.h>
#include <SPI.h>
#include "lv_fs_if.h"

// INCLUDE PER IO EXPANDER - USA I2C ESISTENTE (MANTIENI SOLO PER SD CARD)
#include "driver/i2c.h"
#include "../src/port/esp_io_expander.h"
#include "../src/port/esp_io_expander_ch422g.h"

// AGGIUNTO PER IL CONTATORE
#include <Preferences.h>

// ============================================
// CONFIGURAZIONE COMUNICAZIONE SERIALE VALVOLE (NUOVO)
// ============================================
#define USE_MAIN_SERIAL false  // false = usa Serial2 dedicata
#define VALVE_SERIAL_BAUDRATE 9600
#define VALVE_SERIAL_RX_PIN 43  // Pin RX per RS485 (GPIO43)
#define VALVE_SERIAL_TX_PIN 44  // Pin TX per RS485 (GPIO44)

// Protocollo comunicazione valvole seriale
#define VALVE_CMD_START 0xAA
#define VALVE_CMD_STOP 0xBB
#define VALVE_CMD_RESET 0xCC
#define VALVE_CMD_STATUS 0xDD

// Codici tipo acqua per seriale
#define ACQUA_LISCIA_SERIAL 0x01
#define ACQUA_FRESCA_SERIAL 0x02
#define ACQUA_FRIZZANTE_SERIAL 0x03

// ============================================
// CONFIGURAZIONE LOGO AGGIORNATA
// ============================================
#define LOGO_SPIFFS_PATH "/logo.bmp"
#define LOGO_SD_PATH "/logo.bmp"
#define LOGO_MAX_WIDTH 150  // 400
#define LOGO_MAX_HEIGHT 130
#define LOGO_MAX_SIZE 500000  // 500KB max

// ============================================
// SISTEMA ANTI-FREEZE E MONITORAGGIO
// ============================================
#define LVGL_TIMEOUT_MS 200
#define I2C_TIMEOUT_MS 100
#define WDT_TIMEOUT_SECONDS 10
#define SYSTEM_HEALTH_CHECK_INTERVAL 30000
#define MEMORY_WARNING_THRESHOLD 50000

// Mutex globale per I2C (solo per IO expander ora)
SemaphoreHandle_t i2c_mutex = NULL;

// Flag per stato WDT
bool wdt_initialized = false;

extern const lv_img_dsc_t acqua_fresca, acqua_frizzante, acqua_liscia, bicchiere, bottiglina, bottiglia, acqua_fresca_selezionata, acqua_frizzante_selezionata, acqua_liscia_selezionata, bicchiere_selezionata, bottiglina_selezionata, bottiglia_selezionata, freccia_cerchiata, impostazioni_allarme, impostazioni_aspetto, impostazioni_bicchiere, impostazioni_contatore, impostazioni_schermo, ingranaggio, ingranaggio_cerchiato, spunta, spunta_cerchiata;

enum AlarmLevel {
  ALARM_NONE = 0,
  ALARM_WARNING = 1,   // Preavviso
  ALARM_CRITICAL = 2,  // Critico
  ALARM_EXPIRED = 3    // Scaduto
};
AlarmLevel livelloAllarme;

struct AlarmSettings {
  bool enabled = true;
  uint8_t warning_days_before = 7;   // Preavviso 7 giorni prima
  uint8_t critical_days_before = 3;  // Critico 3 giorni prima
  bool sound_enabled = true;
  bool visual_notification = true;
  uint8_t notification_frequency = 1;  // 1=Ogni avvio, 2=Una volta al giorno, 3=Una volta a settimana
  uint32_t last_notification_day = 0;
  bool snooze_active = false;
  uint32_t snooze_until_day = 0;
} alarm_settings;

// VARIABILI GLOBALI PER IL CONTATORE
float litri_erogati_totali = 0.0;
Preferences preferences;

// VARIABILI GLOBALI PER GESTIONE FILTRI
uint32_t giorni_installazione_filtro = 0;
uint8_t limite_mesi_filtro = 6;
bool filtro_scaduto_notificato = false;
bool controllo_filtro_attivo = true;

// Oggetti LVGL per schermata allarmi
lv_obj_t *schermo_allarmi = NULL;
lv_obj_t *switch_allarmi_attivi = NULL;
lv_obj_t *slider_giorni_preavviso = NULL;
lv_obj_t *label_giorni_preavviso = NULL;
lv_obj_t *slider_giorni_critico = NULL;
lv_obj_t *label_giorni_critico = NULL;
lv_obj_t *switch_audio = NULL;
lv_obj_t *switch_visual = NULL;
lv_obj_t *dropdown_frequenza = NULL;
lv_obj_t *label_stato_allarme = NULL;

// OGGETTI LVGL PER SCHERMATA FILTRI
lv_obj_t *schermo_filtri = NULL;
lv_obj_t *label_giorni_filtro = NULL;
lv_obj_t *label_stato_filtro = NULL;
lv_obj_t *slider_limite_mesi = NULL;
lv_obj_t *label_limite_mesi = NULL;
lv_obj_t *switch_controllo_automatico = NULL;

// ============================================
// FUNZIONI GESTIONE DATI ALLARMI
// ============================================

void carica_impostazioni_allarmi() {
  Serial.println("Caricamento impostazioni allarmi...");
  preferences.begin("alarm_system", false);

  alarm_settings.enabled = preferences.getBool("enabled", true);
  alarm_settings.warning_days_before = preferences.getUChar("warn_days", 7);
  alarm_settings.critical_days_before = preferences.getUChar("crit_days", 3);
  alarm_settings.sound_enabled = preferences.getBool("sound", true);
  alarm_settings.visual_notification = preferences.getBool("visual", true);
  alarm_settings.notification_frequency = preferences.getUChar("frequency", 1);
  alarm_settings.last_notification_day = preferences.getUInt("last_notif", 0);
  alarm_settings.snooze_active = preferences.getBool("snooze", false);
  alarm_settings.snooze_until_day = preferences.getUInt("snooze_until", 0);

  preferences.end();

  Serial.printf("Allarmi caricati: enabled=%s, warn=%d, crit=%d giorni\n",
                alarm_settings.enabled ? "SÌ" : "NO",
                alarm_settings.warning_days_before,
                alarm_settings.critical_days_before);
}

void salva_impostazioni_allarmi() {
  Serial.println("Salvataggio impostazioni allarmi...");
  preferences.begin("alarm_system", false);

  preferences.putBool("enabled", alarm_settings.enabled);
  preferences.putUChar("warn_days", alarm_settings.warning_days_before);
  preferences.putUChar("crit_days", alarm_settings.critical_days_before);
  preferences.putBool("sound", alarm_settings.sound_enabled);
  preferences.putBool("visual", alarm_settings.visual_notification);
  preferences.putUChar("frequency", alarm_settings.notification_frequency);
  preferences.putUInt("last_notif", alarm_settings.last_notification_day);
  preferences.putBool("snooze", alarm_settings.snooze_active);
  preferences.putUInt("snooze_until", alarm_settings.snooze_until_day);

  preferences.end();
  Serial.println("✅ Impostazioni allarmi salvate");
}

// ============================================
// LOGICA SISTEMA ALLARMI
// ============================================

AlarmLevel get_current_alarm_level() {
  if (!alarm_settings.enabled || !controllo_filtro_attivo || giorni_installazione_filtro == 0) {
    return ALARM_NONE;
  }

  // Controlla snooze
  uint32_t today = ottieni_giorni_correnti();
  if (alarm_settings.snooze_active && today < alarm_settings.snooze_until_day) {
    return ALARM_NONE;
  }

  int giorni_dall_installazione = calcola_giorni_dall_installazione();
  int giorni_limite = limite_mesi_filtro * 30;
  int giorni_rimanenti = giorni_limite - giorni_dall_installazione;

  if (giorni_rimanenti < 0) {
    return ALARM_EXPIRED;
  } else if (giorni_rimanenti <= alarm_settings.critical_days_before) {
    return ALARM_CRITICAL;
  } else if (giorni_rimanenti <= alarm_settings.warning_days_before) {
    return ALARM_WARNING;
  }

  return ALARM_NONE;
}

const char *get_alarm_level_text(AlarmLevel level) {
  switch (level) {
    case ALARM_WARNING: return "⚠️ PREAVVISO";
    case ALARM_CRITICAL: return "🔴 CRITICO";
    case ALARM_EXPIRED: return "❌ SCADUTO";
    default: return "✅ OK";
  }
}

uint32_t get_alarm_level_color(AlarmLevel level) {
  switch (level) {
    case ALARM_WARNING: return 0xFF8800;   // Arancione
    case ALARM_CRITICAL: return 0xFF4444;  // Rosso
    case ALARM_EXPIRED: return 0x8B0000;   // Rosso scuro
    default: return 0x00AA00;              // Verde
  }
}

bool should_show_notification() {
  AlarmLevel level = get_current_alarm_level();
  if (level == ALARM_NONE) return false;

  uint32_t today = ottieni_giorni_correnti();

  // Controlla frequenza notifiche
  switch (alarm_settings.notification_frequency) {
    case 1:  // Ogni avvio
      return true;
    case 2:  // Una volta al giorno
      return (today != alarm_settings.last_notification_day);
    case 3:  // Una volta a settimana
      return (today - alarm_settings.last_notification_day >= 7);
    default:
      return false;
  }
}

void set_notification_shown() {
  alarm_settings.last_notification_day = ottieni_giorni_correnti();
  salva_impostazioni_allarmi();
}

void snooze_alarm(uint8_t days) {
  alarm_settings.snooze_active = true;
  alarm_settings.snooze_until_day = ottieni_giorni_correnti() + days;
  salva_impostazioni_allarmi();
  Serial.printf("Allarme posticipato di %d giorni\n", days);
}

void clear_snooze() {
  alarm_settings.snooze_active = false;
  alarm_settings.snooze_until_day = 0;
  salva_impostazioni_allarmi();
  Serial.println("Snooze rimosso");
}

// ============================================
// CALLBACK CONTROLLI UI
// ============================================

static void switch_allarmi_cb(lv_event_t *e) {
  lv_obj_t *sw = lv_event_get_target(e);
  alarm_settings.enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);

  if (!alarm_settings.enabled) {
    clear_snooze();  // Rimuovi snooze se allarmi disattivati
  }

  salva_impostazioni_allarmi();
  aggiorna_display_allarmi();

  Serial.printf("Allarmi filtro: %s\n", alarm_settings.enabled ? "ATTIVATI" : "DISATTIVATI");
}

static void slider_preavviso_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  alarm_settings.warning_days_before = lv_slider_get_value(slider);

  // Assicurati che il preavviso sia sempre >= critico
  if (alarm_settings.warning_days_before < alarm_settings.critical_days_before) {
    alarm_settings.critical_days_before = alarm_settings.warning_days_before;
    if (slider_giorni_critico) {
      lv_slider_set_value(slider_giorni_critico, alarm_settings.critical_days_before, LV_ANIM_OFF);
    }
  }

  safe_lvgl_operation([&]() {
    static char buf[50];
    snprintf(buf, sizeof(buf), "Preavviso: %d giorni", alarm_settings.warning_days_before);
    lv_label_set_text(label_giorni_preavviso, buf);
  });

  salva_impostazioni_allarmi();
  aggiorna_display_allarmi();
}

static void slider_critico_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  alarm_settings.critical_days_before = lv_slider_get_value(slider);

  // Assicurati che il critico sia sempre <= preavviso
  if (alarm_settings.critical_days_before > alarm_settings.warning_days_before) {
    alarm_settings.warning_days_before = alarm_settings.critical_days_before;
    if (slider_giorni_preavviso) {
      lv_slider_set_value(slider_giorni_preavviso, alarm_settings.warning_days_before, LV_ANIM_OFF);
    }
  }

  safe_lvgl_operation([&]() {
    static char buf[50];
    snprintf(buf, sizeof(buf), "Critico: %d giorni", alarm_settings.critical_days_before);
    lv_label_set_text(label_giorni_critico, buf);
  });

  salva_impostazioni_allarmi();
  aggiorna_display_allarmi();
}

static void dropdown_frequenza_cb(lv_event_t *e) {
  lv_obj_t *dropdown = lv_event_get_target(e);
  alarm_settings.notification_frequency = lv_dropdown_get_selected(dropdown) + 1;

  salva_impostazioni_allarmi();
  aggiorna_display_allarmi();

  const char *freq_names[] = { "Ogni avvio", "Una volta al giorno", "Una volta a settimana" };
  Serial.printf("Frequenza notifiche: %s\n", freq_names[alarm_settings.notification_frequency - 1]);
}

// ============================================
// AGGIORNAMENTO DISPLAY ALLARMI
// ============================================

void aggiorna_display_allarmi() {
  if (!label_stato_allarme) return;

  safe_lvgl_operation([&]() {
    // Aggiorna controlli UI
    if (switch_allarmi_attivi) {
      if (alarm_settings.enabled) {
        lv_obj_add_state(switch_allarmi_attivi, LV_STATE_CHECKED);
      } else {
        lv_obj_clear_state(switch_allarmi_attivi, LV_STATE_CHECKED);
      }
    }

    if (slider_giorni_preavviso) {
      lv_slider_set_value(slider_giorni_preavviso, alarm_settings.warning_days_before, LV_ANIM_OFF);
    }

    if (slider_giorni_critico) {
      lv_slider_set_value(slider_giorni_critico, alarm_settings.critical_days_before, LV_ANIM_OFF);
    }

    AlarmLevel level = get_current_alarm_level();
    static char buffer[200];

    if (!alarm_settings.enabled) {
      lv_label_set_text(label_stato_allarme, "🔕 Allarmi disattivati");
      lv_obj_set_style_text_color(label_stato_allarme, lv_color_hex(0x888888), 0);
      return;
    }

    if (giorni_installazione_filtro == 0) {
      lv_label_set_text(label_stato_allarme, "⚙️ Filtro non configurato");
      lv_obj_set_style_text_color(label_stato_allarme, lv_color_hex(0x888888), 0);
      return;
    }

    const char *status_text = get_alarm_level_text(level);
    uint32_t status_color = get_alarm_level_color(level);

    int giorni_rimanenti = (limite_mesi_filtro * 30) - calcola_giorni_dall_installazione();

    if (level == ALARM_NONE) {
      snprintf(buffer, sizeof(buffer),
               "%s\n\nProssimo allarme tra: %d giorni\n"
               "Preavviso: %d giorni prima\nCritico: %d giorni prima",
               status_text,
               giorni_rimanenti - alarm_settings.warning_days_before,
               alarm_settings.warning_days_before,
               alarm_settings.critical_days_before);
    } else {
      snprintf(buffer, sizeof(buffer),
               "%s\n\nGiorni rimanenti: %d\n"
               "Frequenza notifiche: %s",
               status_text,
               giorni_rimanenti,
               alarm_settings.notification_frequency == 1 ? "Ogni avvio" : alarm_settings.notification_frequency == 2 ? "Una volta al giorno"
                                                                                                                      : "Una volta a settimana");
    }

    // Aggiungi info snooze se attivo
    if (alarm_settings.snooze_active) {
      uint32_t today = ottieni_giorni_correnti();
      int snooze_days = alarm_settings.snooze_until_day - today;
      if (snooze_days > 0) {
        static char snooze_info[50];
        snprintf(snooze_info, sizeof(snooze_info), "\n\n😴 Posticipato per %d giorni", snooze_days);
        strcat(buffer, snooze_info);
      }
    }

    lv_label_set_text(label_stato_allarme, buffer);
    lv_obj_set_style_text_color(label_stato_allarme, lv_color_hex(status_color), 0);
  });
}

// ============================================
// CONTROLLO PERIODICO ALLARMI (DA INTEGRARE NEL LOOP)
// ============================================

void controlla_allarmi_sistema() {
  static unsigned long ultimo_controllo_allarmi = 0;
  static bool allarmi_inizializzati = false;

  // Inizializzazione ritardata
  if (!allarmi_inizializzati && millis() > 15000) {
    allarmi_inizializzati = true;
    Serial.println("Inizializzazione sistema allarmi...");
    carica_impostazioni_allarmi();
    Serial.println("✅ Sistema allarmi inizializzato");
  }

  // Controllo ogni 60 secondi
  if (allarmi_inizializzati && (millis() - ultimo_controllo_allarmi > 60000)) {
    ultimo_controllo_allarmi = millis();

    if (should_show_notification()) {
      Serial.println("🔔 Mostrando notifica allarme filtro");
      mostra_notifica_allarme_avanzata();
    }
  }
}

class SystemMonitor {
private:
  unsigned long last_heartbeat = 0;
  unsigned long last_memory_check = 0;
  bool system_healthy = true;
  uint32_t min_free_heap = UINT32_MAX;

public:
  void init() {
    last_heartbeat = millis();
    min_free_heap = ESP.getFreeHeap();
  }

  void heartbeat() {
    last_heartbeat = millis();
    system_healthy = true;

    // Controlla memoria ogni 10 secondi
    if (millis() - last_memory_check > 10000) {
      check_memory();
      last_memory_check = millis();
    }
  }

  void check_memory() {
    uint32_t free_heap = ESP.getFreeHeap();
    if (free_heap < min_free_heap) {
      min_free_heap = free_heap;
    }

    if (free_heap < MEMORY_WARNING_THRESHOLD) {
      Serial.printf("⚠️ MEMORIA BASSA: %d bytes (min: %d)\n", free_heap, min_free_heap);
    }
  }

  void check_health() {
    if (millis() - last_heartbeat > 5000) {
      Serial.println("❌ Sistema bloccato, tentativo recovery...");
      system_recovery();
    }
  }

  void system_recovery() {
    Serial.println("🔄 Eseguendo recovery del sistema...");

    // Forza unlock LVGL se bloccato
    for (int i = 0; i < 5; i++) {
      lvgl_port_unlock();
    }

    // Attendi un po' e poi restart
    delay(1000);
    ESP.restart();
  }

  void print_stats() {
    Serial.printf("💾 Heap: %d/%d bytes, PSRAM: %d bytes\n",
                  ESP.getFreeHeap(), min_free_heap, ESP.getFreePsram());
  }
};

SystemMonitor monitor;

// ============================================
// CLASSE GESTORE COMUNICAZIONE SERIALE VALVOLE (NUOVO)
// ============================================

class SerialValveManager {
private:
  static HardwareSerial *valve_serial;
  static bool initialized;
  static unsigned long last_command_time;
  static const unsigned long MIN_COMMAND_INTERVAL = 100;  // ms tra comandi

public:
  // Inizializzazione
  static bool init() {
    Serial.println("=== INIT VALVOLE SERIALE ===");

#if USE_MAIN_SERIAL
    valve_serial = &Serial;
    Serial.println("✅ Usando Serial principale per valvole");
    Serial.println("⚠️ Debug e valvole condivideranno la stessa seriale!");
#else
    valve_serial = &Serial2;
    Serial2.begin(VALVE_SERIAL_BAUDRATE, SERIAL_8N1, VALVE_SERIAL_RX_PIN, VALVE_SERIAL_TX_PIN);
    delay(100);
    Serial.printf("✅ RS485 integrato inizializzato: %d baud, RX=GPIO%d, TX=GPIO%d\n",
                  VALVE_SERIAL_BAUDRATE, VALVE_SERIAL_RX_PIN, VALVE_SERIAL_TX_PIN);
#endif

    initialized = true;
    last_command_time = 0;

    // Test di comunicazione
    if (sendResetCommand()) {
      Serial.println("✅ Comunicazione con valvole OK");
      return true;
    } else {
      Serial.println("⚠️ Valvole non rispondono - modalità simulazione");
      return false;
    }
  }

  // Invio comandi con checksum
  static bool sendCommand(uint8_t command, uint8_t data = 0x00) {
    if (!initialized || !valve_serial) {
      Serial.println("❌ Seriale valvole non inizializzata");
      return false;
    }

    // Rate limiting
    unsigned long now = millis();
    if (now - last_command_time < MIN_COMMAND_INTERVAL) {
      delay(MIN_COMMAND_INTERVAL - (now - last_command_time));
    }

    // Protocollo: [HEADER][COMMAND][DATA][CHECKSUM][FOOTER]
    uint8_t packet[5];
    packet[0] = 0xFF;                              // Header
    packet[1] = command;                           // Comando
    packet[2] = data;                              // Dati
    packet[3] = calculateChecksum(command, data);  // Checksum
    packet[4] = 0xFE;                              // Footer

    // Invia pacchetto
    size_t bytes_sent = valve_serial->write(packet, 5);
    valve_serial->flush();  // Assicura invio completo

    last_command_time = millis();

    Serial.printf("📡 Valvole TX: [0x%02X][0x%02X][0x%02X][0x%02X][0x%02X]\n",
                  packet[0], packet[1], packet[2], packet[3], packet[4]);

    return bytes_sent == 5;
  }

  // Comandi specifici
  static bool sendStartCommand(uint8_t tipo_acqua) {
    uint8_t acqua_code;
    const char *acqua_name;

    switch (tipo_acqua) {
      case 0:
        acqua_code = ACQUA_LISCIA_SERIAL;
        acqua_name = "LISCIA";
        break;
      case 1:
        acqua_code = ACQUA_FRESCA_SERIAL;
        acqua_name = "FRESCA";
        break;
      case 2:
        acqua_code = ACQUA_FRIZZANTE_SERIAL;
        acqua_name = "FRIZZANTE";
        break;
      default:
        acqua_code = ACQUA_LISCIA_SERIAL;
        acqua_name = "LISCIA(default)";
        break;
    }

    Serial.printf("🚰 START erogazione: %s (codice 0x%02X)\n", acqua_name, acqua_code);
    return sendCommand(VALVE_CMD_START, acqua_code);
  }

  static bool sendStopCommand() {
    Serial.println("🛑 STOP erogazione");
    return sendCommand(VALVE_CMD_STOP);
  }

  static bool sendResetCommand() {
    Serial.println("🔄 RESET sistema valvole");
    return sendCommand(VALVE_CMD_RESET);
  }

  static uint8_t calculateChecksum(uint8_t command, uint8_t data) {
    return (command ^ data) & 0xFF;
  }
};

// Inizializzazione variabili statiche
HardwareSerial *SerialValveManager::valve_serial = nullptr;
bool SerialValveManager::initialized = false;
unsigned long SerialValveManager::last_command_time = 0;

// ============================================
// FUNZIONI HELPER THREAD-SAFE
// ============================================

bool safe_lvgl_operation(std::function<void()> operation) {
  if (lvgl_port_lock(LVGL_TIMEOUT_MS)) {
    try {
      operation();
      lvgl_port_unlock();
      return true;
    } catch (...) {
      Serial.println("⚠️ Eccezione in operazione LVGL!");
      lvgl_port_unlock();
      return false;
    }
  }
  Serial.println("⚠️ LVGL lock timeout!");
  return false;
}

bool safe_i2c_operation(std::function<esp_err_t()> operation) {
  if (i2c_mutex && xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(I2C_TIMEOUT_MS))) {
    esp_err_t result = operation();
    xSemaphoreGive(i2c_mutex);
    return result == ESP_OK;
  }
  Serial.println("⚠️ I2C mutex timeout!");
  return false;
}

void safe_screen_delete(lv_obj_t *&screen) {
  if (screen) {
    safe_lvgl_operation([&]() {
      lv_obj_del(screen);
      screen = NULL;
    });
  }
}

#define MAX_LARGHEZZA 800
#define MAX_ALTEZZA 480

#define SIZE_ICON_SMALL 64
#define SIZE_ICON_CERCHIATA 99
#define SIZE_ICON_BIG 128

// definizione oggetti LVGL
lv_obj_t *schermo1;
lv_obj_t *schermo2;
lv_obj_t *schermo_impostazioni_orologio;
lv_obj_t *schermo_impostazioni_erogazione;

// ============================================
// FUNZIONE HELPER PER CREARE LAYOUT STANDARD IMPOSTAZIONI
// ============================================

lv_obj_t *crea_schermata_impostazioni_standard(const char *titolo) {
  lv_obj_t *schermo = lv_obj_create(NULL);

  // Freccia indietro (posizione standard)
  lv_obj_t *freccia_indietro = lv_imgbtn_create(schermo);
  lv_imgbtn_set_src(freccia_indietro, LV_IMGBTN_STATE_RELEASED, NULL, &freccia_cerchiata, NULL);
  lv_obj_set_size(freccia_indietro, SIZE_ICON_CERCHIATA, SIZE_ICON_CERCHIATA);
  lv_obj_align(freccia_indietro, LV_ALIGN_TOP_LEFT, 20, 20);
  lv_obj_add_event_cb(
    freccia_indietro, [](lv_event_t *e) {
      safe_lvgl_operation([&]() {
        lv_scr_load_anim(schermo2, LV_SCR_LOAD_ANIM_FADE_IN, 150, 0, false);
      });
    },
    LV_EVENT_CLICKED, NULL);

  // Titolo centrato (stesso stile di FLOW e IMPOSTAZIONI)
  lv_obj_t *label_titolo = lv_label_create(schermo);
  lv_label_set_text(label_titolo, titolo);
  lv_obj_align(label_titolo, LV_ALIGN_TOP_MID, 0, 50);                   // Stessa posizione Y di FLOW
  lv_obj_set_style_text_font(label_titolo, &lv_font_montserrat_48, 0);   // Stesso font
  lv_obj_set_style_text_color(label_titolo, lv_color_hex(0x0F73B5), 0);  // Stesso colore blu

  // Linea separatrice (identica alle altre schermate)
  lv_obj_t *linea_separatrice = lv_obj_create(schermo);
  lv_obj_set_size(linea_separatrice, MAX_LARGHEZZA - 40, 3);
  lv_obj_align(linea_separatrice, LV_ALIGN_TOP_MID, 0, 150);  // Stessa posizione Y
  lv_obj_set_style_bg_color(linea_separatrice, lv_color_hex(0x0F73B5), 0);
  lv_obj_set_style_border_width(linea_separatrice, 0, 0);
  lv_obj_set_style_radius(linea_separatrice, 0, 0);
  lv_obj_clear_flag(linea_separatrice, LV_OBJ_FLAG_CLICKABLE);

  return schermo;
}

// ============================================
// CONFIGURAZIONE SD CON IO EXPANDER - I2C ESISTENTE (MANTIENI)
// ============================================
#define TP_RST 1
#define LCD_BL 2
#define LCD_RST 3
#define SD_CS 4
#define USB_SEL 5

#define EXAMPLE_I2C_ADDR 0x24
#define EXAMPLE_I2C_SDA_PIN 8
#define EXAMPLE_I2C_SCL_PIN 9

#define SD_MOSI 11
#define SD_CLK 12
#define SD_MISO 13

esp_io_expander_handle_t io_expander = NULL;

using namespace esp_panel::drivers;
using namespace esp_panel::board;

// variabili globali
uint8_t tempoBottiglia = 10;
uint8_t tempoBicchiere = 5;
bool bottigliaBicchiere = 0;
uint8_t tempoContenitore[] = { 10, 20, 30 };
uint8_t tipoAcqua = 0;
bool lockErogazione = 0;
bool sd_disponibile = 0;
bool valvole_disponibili = false;
bool spiffs_disponibile = false;

// VARIABILI GLOBALI LOGO MANAGER
static lv_obj_t *img_logo_obj = nullptr;
static uint16_t *logo_rgb565_data = nullptr;
static lv_img_dsc_t logo_img_dsc;
static bool logo_loaded = false;

lv_obj_t *sliderBottiglia, *etichettaSliderBottiglia;
lv_obj_t *sliderBicchiere, *etichettaSliderBicchiere;
lv_obj_t *bottone_bicchiere;
lv_obj_t *debug_label = NULL;
lv_obj_t *bottone_acqua_liscia = NULL;
lv_obj_t *bottone_acqua_fresca = NULL;
lv_obj_t *bottone_acqua_frizzante = NULL;
lv_obj_t *bottone_bottiglina = NULL;
lv_obj_t *bottone_bottiglia = NULL;
lv_obj_t *cerchio_selezione = NULL;
lv_obj_t *arco_timer = NULL;
lv_obj_t *bottone_impostazioni_schermo = NULL;
lv_obj_t *bottone_impostazioni_bicchiere = NULL;
lv_obj_t *bottone_impostazioni_contatore = NULL;
lv_obj_t *bottone_impostazioni_allarme = NULL;
lv_obj_t *label_orologio = NULL;
lv_obj_t *slider_ore, *label_ore = NULL;
lv_obj_t *slider_minuti, *label_minuti = NULL;
lv_obj_t *slider_bicchiere_erogazione, *label_bicchiere_erogazione;
lv_obj_t *slider_bottiglina_erogazione, *label_bottiglina_erogazione;
lv_obj_t *slider_bottiglia_erogazione, *label_bottiglia_erogazione;

// OGGETTI LVGL PER IL CONTATORE
lv_obj_t *schermo_contatore = NULL;
lv_obj_t *label_litri = NULL;
lv_obj_t *label_bottiglie = NULL;
lv_obj_t *label_denaro = NULL;
lv_obj_t *label_ultima_erogazione = NULL;

// prototipi delle funzioni
/* void callback_bottone_acqua_liscia(lv_event_t *evento);
void callback_bottone_acqua_fresca(lv_event_t *evento);
void callback_bottone_acqua_frizzante(lv_event_t *evento);
void callback_bottone_bicchiere(lv_event_t *evento);
void callback_bottone_bottiglina(lv_event_t *evento);
void callback_bottone_bottiglia(lv_event_t *evento);
void callback_bottone_impostazioni_schermo(lv_event_t *evento);
void callback_bottone_impostazioni_bicchiere(lv_event_t *evento);
void callback_bottone_impostazioni_aspetto(lv_event_t *evento);
void callback_bottone_impostazioni_allarme(lv_event_t *evento);
void callback_bottone_impostazioni_contatore(lv_event_t *evento); */
static void slider_event_cb(lv_event_t *evento);
void callback_bottone_impostazioni_erogazione(lv_event_t *evento);
static void slider_event_erogazione_cb(lv_event_t *e);

// Nuovi prototipi per bottoni + e -
static void btn_plus_cb(lv_event_t *e);
static void btn_minus_cb(lv_event_t *e);

void deseleziona_bottoni();
void evidenzia_bottone(lv_obj_t *bersaglio);
void aggiorna_debug_label(uint8_t *valore);
void erogazione(uint8_t tempo, lv_obj_t *target, uint8_t tipo_contenitore);
static void gesture_cb(lv_event_t *e);
lv_obj_t *crea_schermata_generica(const char *titolo);
void cancella_arco();
void aggiorna_orologio();
void setup_rtc();
static void salva_orario_cb(lv_event_t *e);
void aggiorna_label_orario();
void mostra_errore_sd(const char *msg);
void setup_logo_system();

// Prototipi funzioni filtri
void controlla_stato_filtro();
void carica_dati_filtro();
void salva_dati_filtro();
void mostra_notifica_filtro_scaduto();
void aggiorna_display_filtro();

// ============================================
// CLASSE HELPER PER OPERAZIONI SD (THREAD-SAFE)
// ============================================
class SDCardManager {
private:
  static bool sd_cs_fixed_low;  // Flag per CS fisso LOW

public:
  static void setCSFixedLow(bool fixed) {
    sd_cs_fixed_low = fixed;
    Serial.printf("🔧 SDCardManager: CS fisso LOW = %s\n", fixed ? "SÌ" : "NO");
  }

  static bool operationWrapper(bool (*operation)()) {
    if (io_expander == NULL) {
      Serial.println("❌ IO expander NULL in operationWrapper");
      return false;
    }

    // Se CS è fisso LOW, non toccarlo
    if (sd_cs_fixed_low) {
      return operation();
    }

    return safe_i2c_operation([&]() -> esp_err_t {
      esp_io_expander_set_level(io_expander, BIT(SD_CS), 0);
      delayMicroseconds(10);

      bool result = operation();

      delayMicroseconds(10);
      esp_io_expander_set_level(io_expander, BIT(SD_CS), 1);

      return result ? ESP_OK : ESP_FAIL;
    });
  }

  static bool exists(const char *path) {
    Serial.printf("SDCardManager::exists - Controllo file: %s\n", path);

    // Se CS è fisso LOW, accesso diretto
    if (sd_cs_fixed_low) {
      bool result = SD.exists(path);
      Serial.printf("SD.exists() diretto (CS fisso): %s\n", result ? "TROVATO" : "NON TROVATO");
      return result;
    }

    // Altrimenti usa wrapper
    bool result = SD.exists(path);
    Serial.printf("SD.exists() diretto: %s\n", result ? "TROVATO" : "NON TROVATO");
    return result;
  }

  static File open(const char *path, const char *mode = FILE_READ) {
    Serial.printf("SDCardManager::open - Apertura: %s, mode: %s\n", path, mode);

    // Se CS è fisso LOW, accesso diretto
    if (sd_cs_fixed_low) {
      File file = SD.open(path, mode);
      if (file) {
        Serial.printf("File aperto (CS fisso), size: %d\n", file.size());
      } else {
        Serial.println("❌ Apertura file fallita (CS fisso)");
      }
      return file;
    }

    // Altrimenti accesso normale
    File file = SD.open(path, mode);
    if (file) {
      Serial.printf("File aperto con successo, size: %d\n", file.size());
    } else {
      Serial.println("❌ Apertura file fallita");
    }

    return file;
  }
};

// Inizializza variabile statica
bool SDCardManager::sd_cs_fixed_low = false;

// ============================================
// LOGO MANAGER - NUOVA IMPLEMENTAZIONE
// ============================================

class LogoManager {
private:
  static bool verificaBMP(File &file, uint32_t &width, uint32_t &height, uint16_t &bpp) {
    if (!file || file.size() < 54) {
      Serial.println("❌ File troppo piccolo per essere un BMP");
      return false;
    }

    // Leggi header BMP
    uint8_t header[54];
    file.seek(0);
    if (file.readBytes((char *)header, 54) < 54) {
      Serial.println("❌ Impossibile leggere header BMP");
      return false;
    }

    // Verifica signature "BM"
    if (header[0] != 0x42 || header[1] != 0x4D) {
      Serial.printf("❌ Signature BMP non valida: 0x%02X%02X\n", header[0], header[1]);
      return false;
    }

    // Estrai informazioni
    width = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
    height = header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24);
    bpp = header[28] | (header[29] << 8);

    Serial.printf("BMP Info: %lux%lu, %d bpp\n", width, height, bpp);

    // Verifiche di sicurezza
    if (bpp != 24) {
      Serial.printf("❌ Formato non supportato: %d bpp (richiesto 24)\n", bpp);
      return false;
    }

    if (width > LOGO_MAX_WIDTH || height > LOGO_MAX_HEIGHT) {
      Serial.printf("❌ Dimensioni eccessive: %lux%lu (max %dx%d)\n",
                    width, height, LOGO_MAX_WIDTH, LOGO_MAX_HEIGHT);
      return false;
    }

    if (width * height * 2 > LOGO_MAX_SIZE) {
      Serial.printf("❌ File troppo grande: %lu bytes (max %d)\n",
                    width * height * 2, LOGO_MAX_SIZE);
      return false;
    }

    return true;
  }

  static uint16_t *convertiBMPtoRGB565(File &file, uint32_t width, uint32_t height) {
    Serial.println("Conversione BMP -> RGB565...");

    // Header BMP per trovare offset dati
    uint8_t header[54];
    file.seek(0);
    file.readBytes((char *)header, 54);

    uint32_t data_offset = header[10] | (header[11] << 8) | (header[12] << 16) | (header[13] << 24);
    Serial.printf("Data offset: %lu\n", data_offset);

    // Alloca memoria RGB565
    uint32_t rgb565_size = width * height * 2;
    uint16_t *rgb565_data = (uint16_t *)malloc(rgb565_size);
    if (!rgb565_data) {
      Serial.printf("❌ Impossibile allocare %lu bytes per RGB565\n", rgb565_size);
      return nullptr;
    }

    // Buffer per riga BMP (allineamento a 4 bytes)
    uint32_t bytes_per_row = ((width * 3 + 3) / 4) * 4;
    uint8_t *row_buffer = (uint8_t *)malloc(bytes_per_row);
    if (!row_buffer) {
      Serial.println("❌ Impossibile allocare buffer riga");
      free(rgb565_data);
      return nullptr;
    }

    // Salta all'offset dati
    file.seek(data_offset);

    // Converti riga per riga (BMP è bottom-up)
    for (int y = height - 1; y >= 0; y--) {
      if (file.readBytes((char *)row_buffer, bytes_per_row) < bytes_per_row) {
        Serial.printf("❌ Errore lettura riga %d\n", y);
        free(rgb565_data);
        free(row_buffer);
        return nullptr;
      }

      // Converti BGR -> RGB565
      for (uint32_t x = 0; x < width; x++) {
        uint32_t bmp_idx = x * 3;
        uint32_t rgb565_idx = y * width + x;

        uint8_t b = row_buffer[bmp_idx + 0];
        uint8_t g = row_buffer[bmp_idx + 1];
        uint8_t r = row_buffer[bmp_idx + 2];

        // Converti a RGB565
        uint16_t r5 = (r >> 3) & 0x1F;
        uint16_t g6 = (g >> 2) & 0x3F;
        uint16_t b5 = (b >> 3) & 0x1F;

        rgb565_data[rgb565_idx] = (r5 << 11) | (g6 << 5) | b5;
      }

      // Reset watchdog ogni 10 righe
      if (y % 10 == 0 && wdt_initialized) {
        esp_task_wdt_reset();
      }
      yield();
    }

    free(row_buffer);
    Serial.printf("✅ Conversione completata: %lu bytes RGB565\n", rgb565_size);
    return rgb565_data;
  }

public:
  // ============================================
  // CARICA LOGO DA SPIFFS
  // ============================================
  static bool caricaDaSPIFFS() {
    Serial.println("=== CARICAMENTO LOGO DA SPIFFS ===");

    if (!SPIFFS.exists(LOGO_SPIFFS_PATH)) {
      Serial.println("❌ Logo non trovato in SPIFFS");
      return false;
    }

    File logoFile = SPIFFS.open(LOGO_SPIFFS_PATH, "r");
    if (!logoFile) {
      Serial.println("❌ Impossibile aprire logo da SPIFFS");
      return false;
    }

    uint32_t width, height;
    uint16_t bpp;

    if (!verificaBMP(logoFile, width, height, bpp)) {
      logoFile.close();
      return false;
    }

    uint16_t *rgb565_data = convertiBMPtoRGB565(logoFile, width, height);
    logoFile.close();

    if (!rgb565_data) {
      return false;
    }

    return creaOggettoLVGL(rgb565_data, width, height);
  }

  // ============================================
  // COPIA LOGO DA SD A SPIFFS
  // ============================================
  static bool copiaDaSD() {
    Serial.println("=== COPIA LOGO DA SD A SPIFFS ===");

    if (!SDCardManager::exists(LOGO_SD_PATH)) {
      Serial.println("❌ Logo non trovato su SD");
      return false;
    }

    File logoSD = SDCardManager::open(LOGO_SD_PATH, FILE_READ);
    if (!logoSD) {
      Serial.println("❌ Impossibile aprire logo da SD");
      return false;
    }

    uint32_t width, height;
    uint16_t bpp;

    if (!verificaBMP(logoSD, width, height, bpp)) {
      logoSD.close();
      return false;
    }

    // Apri file destinazione su SPIFFS
    File logoSPIFFS = SPIFFS.open(LOGO_SPIFFS_PATH, "w");
    if (!logoSPIFFS) {
      Serial.println("❌ Impossibile creare file su SPIFFS");
      logoSD.close();
      return false;
    }

    // Copia file
    size_t total_bytes = logoSD.size();
    size_t copied_bytes = 0;
    uint8_t buffer[1024];

    logoSD.seek(0);
    Serial.printf("Copiando %d bytes...\n", total_bytes);

    while (logoSD.available() && copied_bytes < total_bytes) {
      size_t to_read = min((size_t)sizeof(buffer), total_bytes - copied_bytes);
      size_t bytes_read = logoSD.readBytes((char *)buffer, to_read);

      if (bytes_read > 0) {
        logoSPIFFS.write(buffer, bytes_read);
        copied_bytes += bytes_read;
      }

      // Reset watchdog
      if (wdt_initialized) {
        esp_task_wdt_reset();
      }
      yield();
    }

    logoSD.close();
    logoSPIFFS.close();

    Serial.printf("✅ Logo copiato: %d bytes\n", copied_bytes);
    return (copied_bytes == total_bytes);
  }

  // ============================================
  // CREA OGGETTO LVGL
  // ============================================
  static bool creaOggettoLVGL(uint16_t *rgb565_data, uint32_t width, uint32_t height) {
    Serial.println("Creazione oggetto LVGL...");

    // Salva i dati globalmente
    if (logo_rgb565_data) {
      free(logo_rgb565_data);
    }
    logo_rgb565_data = rgb565_data;

    // Configura descrittore immagine
    logo_img_dsc.header.always_zero = 0;
    logo_img_dsc.header.w = width;
    logo_img_dsc.header.h = height;
    logo_img_dsc.data_size = width * height * 2;
    logo_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    logo_img_dsc.data = (uint8_t *)rgb565_data;

    // Operazione LVGL thread-safe
    bool success = safe_lvgl_operation([&]() {
      // Rimuovi logo precedente
      if (img_logo_obj) {
        lv_obj_del(img_logo_obj);
        img_logo_obj = nullptr;
      }

      // Crea nuovo oggetto immagine
      img_logo_obj = lv_img_create(schermo1);
      if (!img_logo_obj) {
        Serial.println("❌ Impossibile creare oggetto immagine");
        return;
      }

      // Configura posizione e dimensioni
      lv_obj_align(img_logo_obj, LV_ALIGN_TOP_LEFT, 20, 10);
      //lv_obj_set_style_max_width(img_logo_obj, 150, 0);  // Limita larghezza per non sovrapporsi
      lv_obj_set_style_max_height(img_logo_obj, LOGO_MAX_HEIGHT, 0);

      // Imposta sorgente immagine
      lv_img_set_src(img_logo_obj, &logo_img_dsc);

      // Assicurati che sia visibile
      lv_obj_clear_flag(img_logo_obj, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(img_logo_obj);
      lv_obj_invalidate(img_logo_obj);

      Serial.printf("✅ Logo caricato: %lux%lu pixels\n", width, height);
    });

    if (success) {
      logo_loaded = true;
      return true;
    } else {
      free(rgb565_data);
      logo_rgb565_data = nullptr;
      return false;
    }
  }

  // ============================================
  // INIZIALIZZAZIONE AUTOMATICA (VERSIONE SEMPLIFICATA)
  // ============================================
  static bool inizializzaAutomatico() {
    Serial.println("=== INIZIALIZZAZIONE AUTOMATICA LOGO ===");

    // 1. Prova a caricare da SPIFFS
    Serial.println("🔍 Ricerca logo in SPIFFS...");
    if (caricaDaSPIFFS()) {
      Serial.println("✅ Logo caricato da SPIFFS");

      // 2. Se la SD è disponibile, controlla se c'è un aggiornamento
      if (sd_disponibile && SD.totalBytes() > 0) {
        Serial.println("🔍 Controllo aggiornamenti su SD...");
        if (aggiornaAutomaticoDaSD()) {
          Serial.println("✅ Logo aggiornato automaticamente dalla SD");
          // Ricarica il logo aggiornato
          if (caricaDaSPIFFS()) {
            Serial.println("✅ Logo aggiornato visualizzato");
          }
        }
      }
      return true;
    }

    // 3. Se non c'è in SPIFFS, prova a copiare dalla SD
    Serial.println("❌ Logo non trovato in SPIFFS");
    if (sd_disponibile && SD.totalBytes() > 0) {
      Serial.println("🔍 Tentativo copia dalla SD...");
      if (copiaDaSD()) {
        Serial.println("✅ Logo copiato da SD a SPIFFS");
        if (caricaDaSPIFFS()) {
          Serial.println("✅ Logo caricato dopo copia da SD");
          return true;
        }
      }
    }

    Serial.println("❌ Nessun logo disponibile");
    return false;
  }

  // ============================================
  // AGGIORNAMENTO AUTOMATICO DA SD (SILENZIOSO)
  // ============================================
  static bool aggiornaAutomaticoDaSD() {
    Serial.println("=== AGGIORNAMENTO AUTOMATICO DA SD ===");

    if (!SDCardManager::exists(LOGO_SD_PATH)) {
      Serial.println("⚠️ Nessun logo.bmp su SD");
      return false;
    }

    Serial.println("🔍 Logo.bmp trovato su SD - aggiornamento forzato");

    // Rimuovi il vecchio logo da SPIFFS
    if (SPIFFS.exists(LOGO_SPIFFS_PATH)) {
      SPIFFS.remove(LOGO_SPIFFS_PATH);
      Serial.println("🗑️ Vecchio logo rimosso da SPIFFS");
    }

    // Copia sempre il nuovo logo
    if (copiaDaSD()) {
      Serial.println("✅ Nuovo logo copiato da SD");

      // Rimuovi il logo corrente per forzare il reload
      rimuoviLogo();

      return true;
    }

    Serial.println("❌ Errore copia logo da SD");
    return false;
  }

  // ============================================
  // AGGIORNA LOGO DA SD (PER COMPATIBILITÀ)
  // ============================================
  static bool aggiornaLogoSD() {
    // Alias per aggiornaAutomaticoDaSD per compatibilità
    return aggiornaAutomaticoDaSD();
  }

  // ============================================
  // UTILITY
  // ============================================
  static bool logoCaricato() {
    return logo_loaded;
  }

  static void rimuoviLogo() {
    if (img_logo_obj) {
      safe_lvgl_operation([&]() {
        lv_obj_del(img_logo_obj);
        img_logo_obj = nullptr;
      });
    }

    if (logo_rgb565_data) {
      free(logo_rgb565_data);
      logo_rgb565_data = nullptr;
    }

    logo_loaded = false;
    Serial.println("Logo rimosso");
  }

  static void mostraInfo() {
    if (!logo_loaded) {
      Serial.println("Nessun logo caricato");
      return;
    }

    Serial.printf("Logo attuale: %dx%d, %d bytes RGB565\n",
                  logo_img_dsc.header.w,
                  logo_img_dsc.header.h,
                  logo_img_dsc.data_size);
  }
};

// ============================================
// FUNZIONI DI INTERFACCIA LOGO
// ============================================

void callback_aggiorna_logo(lv_event_t *evento) {
  safe_lvgl_operation([&]() {
    static const char *btns[] = { "Annulla", "Aggiorna", "" };
    lv_obj_t *mbox = lv_msgbox_create(NULL, "Aggiorna Logo",
                                      "Vuoi aggiornare il logo\ncon quello presente sulla SD?",
                                      btns, true);
    lv_obj_center(mbox);

    lv_obj_add_event_cb(
      mbox, [](lv_event_t *e) {
        lv_obj_t *obj = lv_event_get_current_target(e);
        uint32_t id = lv_msgbox_get_active_btn(obj);

        if (id == 1) {  // Aggiorna
          if (LogoManager::aggiornaLogoSD()) {
            // Mostra conferma successo
            safe_lvgl_operation([&]() {
              lv_obj_t *success_box = lv_msgbox_create(NULL, "Successo",
                                                       "✅ Logo aggiornato\ncon successo!",
                                                       NULL, true);
              lv_obj_center(success_box);
            });
          } else {
            // Mostra errore
            safe_lvgl_operation([&]() {
              lv_obj_t *error_box = lv_msgbox_create(NULL, "Errore",
                                                     "❌ Impossibile aggiornare\nil logo dalla SD",
                                                     NULL, true);
              lv_obj_center(error_box);
            });
          }
        }

        lv_msgbox_close(obj);
      },
      LV_EVENT_VALUE_CHANGED, NULL);
  });
}

void callback_menu_logo(lv_event_t *evento) {
  safe_lvgl_operation([&]() {
    // Usa la nuova funzione helper
    lv_obj_t *schermo_logo = crea_schermata_impostazioni_standard("GESTIONE LOGO");

    // ============================================
    // CONTENUTO SOTTO LA LINEA (Y > 150)
    // ============================================

    // Info logo corrente (posizionato sotto la linea)
    lv_obj_t *info_logo = lv_label_create(schermo_logo);
    if (LogoManager::logoCaricato()) {
      lv_label_set_text(info_logo, "✅ Logo caricato correttamente");
      lv_obj_set_style_text_color(info_logo, lv_color_hex(0x00AA00), 0);
    } else {
      lv_label_set_text(info_logo, "❌ Nessun logo caricato");
      lv_obj_set_style_text_color(info_logo, lv_color_hex(0xFF0000), 0);
    }
    lv_obj_align(info_logo, LV_ALIGN_CENTER, 0, -60);  // Sotto la linea
    lv_obj_set_style_text_font(info_logo, &lv_font_montserrat_16, 0);

    // Bottoni gestione logo (layout in griglia 2x2)
    // Prima riga bottoni
    lv_obj_t *btn_mostra = lv_btn_create(schermo_logo);
    lv_obj_set_size(btn_mostra, 160, 50);
    lv_obj_align(btn_mostra, LV_ALIGN_CENTER, -90, -10);  // Sinistra
    lv_obj_t *label_mostra = lv_label_create(btn_mostra);
    lv_label_set_text(label_mostra, "Info");
    lv_obj_center(label_mostra);
    lv_obj_set_style_bg_color(btn_mostra, lv_color_hex(0x0088FF), 0);

    lv_obj_add_event_cb(
      btn_mostra, [](lv_event_t *e) {
        LogoManager::mostraInfo();
      },
      LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_aggiorna = lv_btn_create(schermo_logo);
    lv_obj_set_size(btn_aggiorna, 160, 50);
    lv_obj_align(btn_aggiorna, LV_ALIGN_CENTER, 90, -10);  // Destra
    lv_obj_t *label_aggiorna = lv_label_create(btn_aggiorna);
    lv_label_set_text(label_aggiorna, "Aggiorna");
    lv_obj_center(label_aggiorna);
    lv_obj_set_style_bg_color(btn_aggiorna, lv_color_hex(0xFF8800), 0);

    lv_obj_add_event_cb(btn_aggiorna, callback_aggiorna_logo, LV_EVENT_CLICKED, NULL);

    // Seconda riga bottoni
    lv_obj_t *btn_rimuovi = lv_btn_create(schermo_logo);
    lv_obj_set_size(btn_rimuovi, 160, 50);
    lv_obj_align(btn_rimuovi, LV_ALIGN_CENTER, -90, 50);  // Sinistra
    lv_obj_t *label_rimuovi = lv_label_create(btn_rimuovi);
    lv_label_set_text(label_rimuovi, "Rimuovi");
    lv_obj_center(label_rimuovi);
    lv_obj_set_style_bg_color(btn_rimuovi, lv_color_hex(0xFF4444), 0);

    lv_obj_add_event_cb(
      btn_rimuovi, [](lv_event_t *e) {
        safe_lvgl_operation([&]() {
          static const char *btns[] = { "Annulla", "Rimuovi", "" };
          lv_obj_t *mbox = lv_msgbox_create(NULL, "Rimuovi Logo",
                                            "Vuoi rimuovere il logo\ncorrente?",
                                            btns, true);
          lv_obj_center(mbox);

          lv_obj_add_event_cb(
            mbox, [](lv_event_t *e) {
              lv_obj_t *obj = lv_event_get_current_target(e);
              uint32_t id = lv_msgbox_get_active_btn(obj);

              if (id == 1) {  // Rimuovi
                LogoManager::rimuoviLogo();
                SPIFFS.remove(LOGO_SPIFFS_PATH);

                safe_lvgl_operation([&]() {
                  lv_obj_t *success_box = lv_msgbox_create(NULL, "Successo",
                                                           "✅ Logo rimosso\ncon successo!",
                                                           NULL, true);
                  lv_obj_center(success_box);
                });
              }

              lv_msgbox_close(obj);
            },
            LV_EVENT_VALUE_CHANGED, NULL);
        });
      },
      LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_test_sd = lv_btn_create(schermo_logo);
    lv_obj_set_size(btn_test_sd, 160, 50);
    lv_obj_align(btn_test_sd, LV_ALIGN_CENTER, 90, 50);  // Destra
    lv_obj_t *label_test_sd = lv_label_create(btn_test_sd);
    lv_label_set_text(label_test_sd, "Test SD");
    lv_obj_center(label_test_sd);
    lv_obj_set_style_bg_color(btn_test_sd, lv_color_hex(0x888888), 0);

    lv_obj_add_event_cb(
      btn_test_sd, [](lv_event_t *e) {
        Serial.println("=== TEST SD CARD MANUALE ===");

        if (!sd_disponibile) {
          Serial.println("❌ SD marcata come non disponibile");
          mostra_errore_sd("SD Card non disponibile");
          return;
        }

        // Verifica stato SD
        Serial.printf("Card Type: %d\n", SD.cardType());
        Serial.printf("Card Size: %lluMB\n", SD.cardSize() / (1024 * 1024));
        Serial.printf("Total Bytes: %lluMB\n", SD.totalBytes() / (1024 * 1024));
        Serial.printf("Used Bytes: %lluMB\n", SD.usedBytes() / (1024 * 1024));

        String status_msg = "";

        if (SD.totalBytes() == 0) {
          status_msg = "❌ ERRORE SD!\n\n";
          status_msg += "Total Bytes: 0MB\n";
          status_msg += "Il file system non è leggibile.\n\n";
          status_msg += "SOLUZIONI:\n";
          status_msg += "1. Formatta SD in FAT32\n";
          status_msg += "2. Prova un'altra SD\n";
          status_msg += "3. Verifica su PC";

          Serial.println(status_msg.c_str());

          delay(100);
          lv_obj_t *mbox = lv_msgbox_create(NULL, "Errore SD", status_msg.c_str(), NULL, true);
          lv_obj_center(mbox);
          return;
        }

        // Test apertura root con accesso diretto (NON thread-safe ma per debug)
        File root = SD.open("/");
        if (!root) {
          Serial.println("❌ Impossibile aprire root SD");
          mostra_errore_sd("Impossibile accedere alla SD");
          return;
        }

        // Lista file
        String file_list = "File sulla SD:\n\n";
        File file = root.openNextFile();
        bool found_logo = false;
        bool found_any_bmp = false;
        int file_count = 0;

        while (file && file_count < 15) {
          String filename = String(file.name());
          file_list += "• " + filename + " (" + String(file.size()) + "b)\n";

          if (filename == "logo.bmp") {
            found_logo = true;
          }

          if (filename.endsWith(".bmp") || filename.endsWith(".BMP")) {
            found_any_bmp = true;
          }

          file = root.openNextFile();
          file_count++;
        }
        root.close();

        if (file_count == 0) {
          file_list = "❌ Nessun file sulla SD\n\n";
          file_list += "Verifica che ci siano file sulla SD!";
        } else {
          file_list += "\n📊 Totale: " + String(file_count) + " file\n";
          file_list += "🖼️ logo.bmp: " + String(found_logo ? "✅ TROVATO" : "❌ MANCANTE") + "\n";
          if (!found_logo && found_any_bmp) {
            file_list += "⚠️ Altri BMP trovati, controlla nome file\n";
          }
          file_list += "💾 Spazio: " + String(SD.totalBytes() / (1024 * 1024)) + "MB";
        }

        Serial.println(file_list.c_str());

        delay(100);
        lv_obj_t *mbox = lv_msgbox_create(NULL, "Test SD Card", file_list.c_str(), NULL, true);
        lv_obj_center(mbox);
      },
      LV_EVENT_CLICKED, NULL);

    lv_scr_load(schermo_logo);
  });
}

void setup_logo_system() {
  Serial.println("Inizializzazione sistema logo...");

  // Attendi che la SD si stabilizzi completamente
  delay(1000);

  // Caricamento automatico logo
  LogoManager::inizializzaAutomatico();
}

// ============================================
// FUNZIONI SEMPLICI PER COMPATIBILITÀ
// ============================================

// Funzione semplice per mostrare info logo corrente
void mostra_logo_su_schermata1() {
  if (LogoManager::logoCaricato()) {
    Serial.println("=== INFO LOGO CORRENTE ===");
    LogoManager::mostraInfo();

    safe_lvgl_operation([&]() {
      static char msg[200];
      snprintf(msg, sizeof(msg),
               "✅ LOGO ATTIVO\n\n"
               "📍 Caricato da: SPIFFS\n"
               "📏 Dimensioni: %dx%d pixel\n"
               "💾 Memoria: %d bytes RGB565\n\n"
               "🔄 Per aggiornare il logo:\n"
               "1. Copia logo.bmp sulla SD\n"
               "2. Riavvia il dispositivo",
               logo_img_dsc.header.w,
               logo_img_dsc.header.h,
               logo_img_dsc.data_size);

      lv_obj_t *mbox = lv_msgbox_create(NULL, "Info Logo", msg, NULL, true);
      lv_obj_center(mbox);
    });
  } else {
    Serial.println("❌ Nessun logo caricato");

    safe_lvgl_operation([&]() {
      static const char *msg =
        "❌ NESSUN LOGO\n\n"
        "Per aggiungere un logo:\n\n"
        "1. Crea un file logo.bmp\n"
        "   (BMP 24-bit, max 400x200px)\n\n"
        "2. Copia sulla SD card\n\n"
        "3. Riavvia il dispositivo\n\n"
        "Il logo verrà caricato\n"
        "automaticamente!";

      lv_obj_t *mbox = lv_msgbox_create(NULL, "Aggiungi Logo", msg, NULL, true);
      lv_obj_center(mbox);
    });
  }
}

// ============================================
// NUOVE FUNZIONI VALVOLE SERIALI (SOSTITUISCONO LE I2C)
// ============================================

bool inizializza_valvole() {
  valvole_disponibili = SerialValveManager::init();
  return valvole_disponibili;
}

bool avvia_erogazione_valvole(uint8_t tipo_acqua) {
  if (valvole_disponibili) {
    return SerialValveManager::sendStartCommand(tipo_acqua);
  } else {
    // Modalità simulazione
    Serial.printf("💧 SIMULAZIONE: START erogazione acqua tipo %d\n", tipo_acqua);
    return true;
  }
}

bool ferma_erogazione_valvole() {
  if (valvole_disponibili) {
    return SerialValveManager::sendStopCommand();
  } else {
    // Modalità simulazione
    Serial.println("💧 SIMULAZIONE: STOP erogazione");
    return true;
  }
}

// ============================================
// FUNZIONI PER GESTIONE DATI CONTATORE
// ============================================

void carica_dati_contatore() {
  preferences.begin("water_counter", false);
  litri_erogati_totali = preferences.getFloat("litri_totali", 0.0);
  preferences.end();
  Serial.printf("Dati caricati: %.2f litri totali\n", litri_erogati_totali);
}

void salva_dati_contatore() {
  preferences.begin("water_counter", false);
  preferences.putFloat("litri_totali", litri_erogati_totali);
  preferences.end();
  Serial.printf("Dati salvati: %.2f litri totali\n", litri_erogati_totali);
}

float calcola_ml_erogati(uint8_t tipo_contenitore) {
  switch (tipo_contenitore) {
    case 0: return 250.0;
    case 1: return 500.0;
    case 2: return 1000.0;
    default: return 0.0;
  }
}

void aggiorna_contatore(uint8_t tipo_contenitore) {
  float ml_erogati = calcola_ml_erogati(tipo_contenitore);
  litri_erogati_totali += ml_erogati / 1000.0;
  salva_dati_contatore();
  Serial.printf("Erogati %.0f ml, totale: %.2f litri\n", ml_erogati, litri_erogati_totali);
}

float calcola_bottiglie_risparmiate() {
  return litri_erogati_totali * 2.0;
}

float calcola_denaro_risparmiato() {
  return calcola_bottiglie_risparmiate() * 0.30;
}

void reset_contatore() {
  litri_erogati_totali = 0.0;
  salva_dati_contatore();
  aggiorna_display_contatore();
  Serial.println("Contatore resettato!");
}

// ============================================
// FUNZIONI PER GESTIONE DATI FILTRI
// ============================================

void carica_dati_filtro() {
  Serial.println("Caricamento dati filtro...");
  preferences.begin("filtro_system", false);
  giorni_installazione_filtro = preferences.getUInt("install_days", 0);
  limite_mesi_filtro = preferences.getUChar("limite_mesi", 6);
  controllo_filtro_attivo = preferences.getBool("controllo_attivo", true);
  filtro_scaduto_notificato = preferences.getBool("notificato", false);
  preferences.end();
  Serial.printf("Dati filtro caricati: install_days=%lu, limite=%d mesi, controllo=%s\n",
                giorni_installazione_filtro, limite_mesi_filtro,
                controllo_filtro_attivo ? "ON" : "OFF");
}

void salva_dati_filtro() {
  Serial.println("Salvataggio dati filtro...");
  preferences.begin("filtro_system", false);
  preferences.putUInt("install_days", giorni_installazione_filtro);
  preferences.putUChar("limite_mesi", limite_mesi_filtro);
  preferences.putBool("controllo_attivo", controllo_filtro_attivo);
  preferences.putBool("notificato", filtro_scaduto_notificato);
  preferences.end();
  Serial.printf("Dati filtro salvati: install_days=%lu, limite=%d mesi\n",
                giorni_installazione_filtro, limite_mesi_filtro);
}

uint32_t ottieni_giorni_correnti() {
  time_t now;
  time(&now);
  return (uint32_t)(now / (24 * 60 * 60));
}

int calcola_giorni_dall_installazione() {
  if (giorni_installazione_filtro == 0) {
    return 0;
  }
  uint32_t giorni_correnti = ottieni_giorni_correnti();
  return (int)(giorni_correnti - giorni_installazione_filtro);
}

float calcola_mesi_dall_installazione() {
  int giorni = calcola_giorni_dall_installazione();
  return (float)giorni / 30.0;
}

bool filtro_scaduto() {
  if (!controllo_filtro_attivo || giorni_installazione_filtro == 0) {
    return false;
  }
  float mesi_trascorsi = calcola_mesi_dall_installazione();
  return mesi_trascorsi >= limite_mesi_filtro;
}

void reset_filtro() {
  giorni_installazione_filtro = ottieni_giorni_correnti();
  filtro_scaduto_notificato = false;
  salva_dati_filtro();
  Serial.println("Filtro resettato - nuova installazione registrata");
}

// ============================================
// FUNZIONI NOTIFICHE FILTRO (THREAD-SAFE)
// ============================================

void mostra_notifica_filtro_scaduto() {
  if (!controllo_filtro_attivo) return;
  if (millis() < 10000) return;  // Sistema non pronto

  safe_lvgl_operation([&]() {
    int giorni = calcola_giorni_dall_installazione();
    float mesi = calcola_mesi_dall_installazione();

    static char messaggio[200];
    snprintf(messaggio, sizeof(messaggio),
             "⚠️ FILTRO SCADUTO!\n\n"
             "Giorni dall'installazione: %d\n"
             "Mesi trascorsi: %.1f\n"
             "Limite impostato: %d mesi\n\n"
             "È necessario sostituire il filtro!",
             giorni, mesi, limite_mesi_filtro);

    static const char *btns[] = { "OK", "Filtro cambiato", "" };
    lv_obj_t *mbox = lv_msgbox_create(NULL, "MANUTENZIONE FILTRO",
                                      messaggio, btns, true);
    lv_obj_center(mbox);

    lv_obj_add_event_cb(
      mbox, [](lv_event_t *e) {
        lv_obj_t *obj = lv_event_get_current_target(e);
        uint32_t id = lv_msgbox_get_active_btn(obj);

        if (id == 1) {
          reset_filtro();
          if (schermo_filtri) {
            aggiorna_display_filtro();
          }
        }

        filtro_scaduto_notificato = true;
        salva_dati_filtro();
        lv_msgbox_close(obj);
      },
      LV_EVENT_VALUE_CHANGED, NULL);
  });
}

void mostra_notifica_filtro_prossimo_scadenza() {
  if (!controllo_filtro_attivo) return;
  if (millis() < 10000) return;

  safe_lvgl_operation([&]() {
    int giorni = calcola_giorni_dall_installazione();
    float mesi = calcola_mesi_dall_installazione();
    int giorni_rimanenti = (limite_mesi_filtro * 30) - giorni;

    static char messaggio[150];
    snprintf(messaggio, sizeof(messaggio),
             "🔔 Promemoria Filtro\n\n"
             "Mesi dall'installazione: %.1f\n"
             "Giorni rimanenti: %d\n\n"
             "Programmare sostituzione filtro!",
             mesi, giorni_rimanenti);

    lv_obj_t *mbox = lv_msgbox_create(NULL, "PROMEMORIA", messaggio, NULL, true);
    lv_obj_center(mbox);
  });
}

// ============================================
// SCHERMATA CONTATORE (THREAD-SAFE)
// ============================================

void aggiorna_display_contatore() {
  if (!label_litri || !label_bottiglie || !label_denaro) return;

  safe_lvgl_operation([&]() {
    static char buffer[100];

    snprintf(buffer, sizeof(buffer), "Litri erogati: %.2f L", litri_erogati_totali);
    lv_label_set_text(label_litri, buffer);

    snprintf(buffer, sizeof(buffer), "Bottiglie risparmiate: %.0f", calcola_bottiglie_risparmiate());
    lv_label_set_text(label_bottiglie, buffer);

    snprintf(buffer, sizeof(buffer), "Denaro risparmiato: %.2f €", calcola_denaro_risparmiato());
    lv_label_set_text(label_denaro, buffer);
  });
}

// ============================================
// SCHERMATA GESTIONE FILTRI (THREAD-SAFE)
// ============================================

void aggiorna_display_filtro() {
  if (!label_giorni_filtro || !label_stato_filtro) return;

  safe_lvgl_operation([&]() {
    static char buffer[150];

    if (giorni_installazione_filtro == 0) {
      lv_label_set_text(label_giorni_filtro, "Filtro non ancora installato");
      lv_label_set_text(label_stato_filtro, "Stato: Non configurato");
      return;
    }

    int giorni = calcola_giorni_dall_installazione();
    float mesi = calcola_mesi_dall_installazione();
    int giorni_rimanenti = (limite_mesi_filtro * 30) - giorni;

    snprintf(buffer, sizeof(buffer),
             "Giorni dall'installazione: %d\n"
             "Mesi trascorsi: %.1f / %d",
             giorni, mesi, limite_mesi_filtro);
    lv_label_set_text(label_giorni_filtro, buffer);

    if (filtro_scaduto()) {
      snprintf(buffer, sizeof(buffer), "Stato: ⚠️ SCADUTO (da %d giorni)", -giorni_rimanenti);
      lv_obj_set_style_text_color(label_stato_filtro, lv_color_hex(0xFF0000), 0);
    } else if (giorni_rimanenti <= 15) {
      snprintf(buffer, sizeof(buffer), "Stato: 🔔 Prossimo alla scadenza (%d giorni)", giorni_rimanenti);
      lv_obj_set_style_text_color(label_stato_filtro, lv_color_hex(0xFF8800), 0);
    } else {
      snprintf(buffer, sizeof(buffer), "Stato: ✅ OK (scadenza tra %d giorni)", giorni_rimanenti);
      lv_obj_set_style_text_color(label_stato_filtro, lv_color_hex(0x00AA00), 0);
    }
    lv_label_set_text(label_stato_filtro, buffer);

    if (slider_limite_mesi) {
      lv_slider_set_value(slider_limite_mesi, limite_mesi_filtro, LV_ANIM_OFF);
    }

    if (switch_controllo_automatico) {
      if (controllo_filtro_attivo) {
        lv_obj_add_state(switch_controllo_automatico, LV_STATE_CHECKED);
      } else {
        lv_obj_clear_state(switch_controllo_automatico, LV_STATE_CHECKED);
      }
    }
  });
}

// ============================================
// HELPER FUNCTION PER CREARE BOTTONI + E - (OTTIMIZZATA)
// ============================================
void crea_bottoni_plus_minus_estremi(lv_obj_t *parent, lv_obj_t *slider, lv_obj_t **btn_plus_ptr, lv_obj_t **btn_minus_ptr) {
  // Bottone "-" a SINISTRA dello slider
  lv_obj_t *btn_minus = lv_btn_create(parent);
  lv_obj_set_size(btn_minus, 50, 40);                                 // Dimensioni ottimizzate
  lv_obj_align_to(btn_minus, slider, LV_ALIGN_OUT_LEFT_MID, -15, 0);  // 15px di distanza
  lv_obj_t *label_minus = lv_label_create(btn_minus);
  lv_label_set_text(label_minus, "-");
  lv_obj_center(label_minus);
  lv_obj_set_style_bg_color(btn_minus, lv_color_hex(0x0F73B5), 0);
  lv_obj_set_style_text_font(label_minus, &lv_font_montserrat_30, 0);

  // Bottone "+" a DESTRA dello slider
  lv_obj_t *btn_plus = lv_btn_create(parent);
  lv_obj_set_size(btn_plus, 50, 40);                                 // Dimensioni ottimizzate
  lv_obj_align_to(btn_plus, slider, LV_ALIGN_OUT_RIGHT_MID, 15, 0);  // 15px di distanza
  lv_obj_t *label_plus = lv_label_create(btn_plus);
  lv_label_set_text(label_plus, "+");
  lv_obj_center(label_plus);
  lv_obj_set_style_bg_color(btn_plus, lv_color_hex(0x0F73B5), 0);
  lv_obj_set_style_text_font(label_plus, &lv_font_montserrat_30, 0);

  // Salva i puntatori se richiesti
  if (btn_plus_ptr) *btn_plus_ptr = btn_plus;
  if (btn_minus_ptr) *btn_minus_ptr = btn_minus;

  // Imposta lo slider come user data dei bottoni per le callback
  lv_obj_set_user_data(btn_plus, slider);
  lv_obj_set_user_data(btn_minus, slider);
}

// ============================================
// CALLBACK PER BOTTONI + E -
// ============================================
static void btn_plus_cb(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  lv_obj_t *slider = (lv_obj_t *)lv_obj_get_user_data(btn);

  if (slider) {
    int current_val = lv_slider_get_value(slider);
    int min_val = lv_slider_get_min_value(slider);
    int max_val = lv_slider_get_max_value(slider);

    if (current_val < max_val) {
      lv_slider_set_value(slider, current_val + 1, LV_ANIM_OFF);
      // Triggerata manualmente l'evento VALUE_CHANGED
      lv_event_send(slider, LV_EVENT_VALUE_CHANGED, NULL);
    }
  }
}

static void btn_minus_cb(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  lv_obj_t *slider = (lv_obj_t *)lv_obj_get_user_data(btn);

  if (slider) {
    int current_val = lv_slider_get_value(slider);
    int min_val = lv_slider_get_min_value(slider);
    int max_val = lv_slider_get_max_value(slider);

    if (current_val > min_val) {
      lv_slider_set_value(slider, current_val - 1, LV_ANIM_OFF);
      // Triggerata manualmente l'evento VALUE_CHANGED
      lv_event_send(slider, LV_EVENT_VALUE_CHANGED, NULL);
    }
  }
}

static void slider_limite_mesi_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  limite_mesi_filtro = lv_slider_get_value(slider);

  safe_lvgl_operation([&]() {
    static char buf[50];
    snprintf(buf, sizeof(buf), "Limite: %d mesi", limite_mesi_filtro);
    lv_label_set_text(label_limite_mesi, buf);
  });

  filtro_scaduto_notificato = false;
  salva_dati_filtro();
  aggiorna_display_filtro();
}

static void switch_controllo_cb(lv_event_t *e) {
  lv_obj_t *sw = lv_event_get_target(e);
  controllo_filtro_attivo = lv_obj_has_state(sw, LV_STATE_CHECKED);

  if (!controllo_filtro_attivo) {
    filtro_scaduto_notificato = false;
  }

  salva_dati_filtro();
  aggiorna_display_filtro();

  Serial.printf("Controllo automatico filtro: %s\n",
                controllo_filtro_attivo ? "ATTIVATO" : "DISATTIVATO");
}

// ============================================
// NOTIFICHE ALLARME AVANZATE
// ============================================

void mostra_notifica_allarme_avanzata() {
  if (!should_show_notification()) return;

  AlarmLevel level = get_current_alarm_level();
  if (level == ALARM_NONE) return;

  safe_lvgl_operation([&]() {
    int giorni_rimanenti = (limite_mesi_filtro * 30) - calcola_giorni_dall_installazione();
    float mesi_trascorsi = calcola_mesi_dall_installazione();

    static char titolo[50];
    static char messaggio[400];

    // Determina titolo e messaggio in base al livello
    switch (level) {
      case ALARM_WARNING:
        strcpy(titolo, "⚠️ PREAVVISO FILTRO");
        snprintf(messaggio, sizeof(messaggio),
                 "Il filtro si avvicina alla scadenza!\n\n"
                 "📅 Mesi dall'installazione: %.1f / %d\n"
                 "⏰ Giorni rimanenti: %d\n\n"
                 "🛠️ Pianifica la sostituzione del filtro\n"
                 "per garantire la qualità dell'acqua.",
                 mesi_trascorsi, limite_mesi_filtro, giorni_rimanenti);
        break;

      case ALARM_CRITICAL:
        strcpy(titolo, "🔴 ALLARME CRITICO");
        snprintf(messaggio, sizeof(messaggio),
                 "ATTENZIONE: Il filtro scade tra pochi giorni!\n\n"
                 "📅 Mesi dall'installazione: %.1f / %d\n"
                 "⏰ Giorni rimanenti: %d\n\n"
                 "🚨 AZIONE RICHIESTA:\n"
                 "Sostituisci IMMEDIATAMENTE il filtro!",
                 mesi_trascorsi, limite_mesi_filtro, giorni_rimanenti);
        break;

      case ALARM_EXPIRED:
        strcpy(titolo, "❌ FILTRO SCADUTO");
        snprintf(messaggio, sizeof(messaggio),
                 "FILTRO SCADUTO!\n\n"
                 "📅 Mesi dall'installazione: %.1f / %d\n"
                 "⚠️ Scaduto da: %d giorni\n\n"
                 "🚫 La qualità dell'acqua potrebbe\n"
                 "essere compromessa!\n\n"
                 "⚡ SOSTITUISCI SUBITO IL FILTRO!",
                 mesi_trascorsi, limite_mesi_filtro, -giorni_rimanenti);
        break;

      default:
        return;  // Non dovrebbe accadere
    }

    // Bottoni dinamici in base al livello di allarme
    static const char *btns_warning[] = { "OK", "Posticipa 3gg", "Sostituito", "" };
    static const char *btns_critical[] = { "OK", "Posticipa 1g", "Sostituito", "" };
    static const char *btns_expired[] = { "OK", "Sostituito", "" };

    const char **btns;
    switch (level) {
      case ALARM_WARNING: btns = btns_warning; break;
      case ALARM_CRITICAL: btns = btns_critical; break;
      case ALARM_EXPIRED: btns = btns_expired; break;
      default: btns = btns_warning; break;
    }

    lv_obj_t *mbox = lv_msgbox_create(NULL, titolo, messaggio, btns, true);
    lv_obj_center(mbox);

    // Colore del titolo in base alla gravità
    lv_obj_t *title = lv_msgbox_get_title(mbox);
    if (title) {
      lv_obj_set_style_text_color(title, lv_color_hex(get_alarm_level_color(level)), 0);
    }

    // Callback per gestire le risposte
    lv_obj_add_event_cb(
      mbox, [](lv_event_t *e) {
        lv_obj_t *obj = lv_event_get_current_target(e);
        uint32_t id = lv_msgbox_get_active_btn(obj);
        AlarmLevel current_level = get_current_alarm_level();

        switch (current_level) {
          case ALARM_WARNING:
            if (id == 1) {  // Posticipa 3 giorni
              snooze_alarm(3);
            } else if (id == 2) {  // Sostituito
              reset_filtro();
              clear_snooze();
              if (schermo_filtri) aggiorna_display_filtro();
            }
            break;

          case ALARM_CRITICAL:
            if (id == 1) {  // Posticipa 1 giorno
              snooze_alarm(1);
            } else if (id == 2) {  // Sostituito
              reset_filtro();
              clear_snooze();
              if (schermo_filtri) aggiorna_display_filtro();
            }
            break;

          case ALARM_EXPIRED:
            if (id == 1) {  // Sostituito
              reset_filtro();
              clear_snooze();
              if (schermo_filtri) aggiorna_display_filtro();
            }
            break;
        }

        set_notification_shown();
        lv_msgbox_close(obj);
      },
      LV_EVENT_VALUE_CHANGED, NULL);
  });
}

void callback_bottone_impostazioni_erogazione(lv_event_t *evento) {
  safe_lvgl_operation([&]() {
    // Usa la nuova funzione helper
    schermo_impostazioni_erogazione = crea_schermata_impostazioni_standard("TEMPO EROGAZIONE");

    // ============================================
    // CONTENUTO SOTTO LA LINEA (Y > 150)
    // ============================================

    // SLIDER BICCHIERE con bottoni agli estremi
    slider_bicchiere_erogazione = lv_slider_create(schermo_impostazioni_erogazione);
    lv_slider_set_range(slider_bicchiere_erogazione, 0, 60);
    lv_slider_set_value(slider_bicchiere_erogazione, tempoContenitore[0], LV_ANIM_OFF);
    lv_obj_set_width(slider_bicchiere_erogazione, 250);
    lv_obj_set_height(slider_bicchiere_erogazione, 25);
    lv_obj_align(slider_bicchiere_erogazione, LV_ALIGN_CENTER, 0, -40);  // Posizione sotto la linea
    lv_obj_set_style_bg_color(slider_bicchiere_erogazione, lv_color_hex(0x0F73B5), LV_PART_INDICATOR);
    lv_obj_add_event_cb(slider_bicchiere_erogazione, slider_event_erogazione_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Bottoni + e - per bicchiere agli estremi
    lv_obj_t *btn_plus_bicchiere, *btn_minus_bicchiere;
    crea_bottoni_plus_minus_estremi(schermo_impostazioni_erogazione, slider_bicchiere_erogazione, &btn_plus_bicchiere, &btn_minus_bicchiere);
    lv_obj_add_event_cb(btn_plus_bicchiere, btn_plus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_minus_bicchiere, btn_minus_cb, LV_EVENT_CLICKED, NULL);

    // Label bicchiere sotto lo slider
    label_bicchiere_erogazione = lv_label_create(schermo_impostazioni_erogazione);
    lv_obj_align_to(label_bicchiere_erogazione, slider_bicchiere_erogazione, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    {
      static char buf[32];
      snprintf(buf, sizeof(buf), "Bicchiere: %d s", tempoContenitore[0]);
      lv_label_set_text(label_bicchiere_erogazione, buf);
    }

    // SLIDER BOTTIGLINA con bottoni agli estremi
    slider_bottiglina_erogazione = lv_slider_create(schermo_impostazioni_erogazione);
    lv_slider_set_range(slider_bottiglina_erogazione, 0, 60);
    lv_slider_set_value(slider_bottiglina_erogazione, tempoContenitore[1], LV_ANIM_OFF);
    lv_obj_set_width(slider_bottiglina_erogazione, 250);
    lv_obj_set_height(slider_bottiglina_erogazione, 25);
    lv_obj_align(slider_bottiglina_erogazione, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(slider_bottiglina_erogazione, lv_color_hex(0x0F73B5), LV_PART_INDICATOR);
    lv_obj_add_event_cb(slider_bottiglina_erogazione, slider_event_erogazione_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Bottoni + e - per bottiglina agli estremi
    lv_obj_t *btn_plus_bottiglina, *btn_minus_bottiglina;
    crea_bottoni_plus_minus_estremi(schermo_impostazioni_erogazione, slider_bottiglina_erogazione, &btn_plus_bottiglina, &btn_minus_bottiglina);
    lv_obj_add_event_cb(btn_plus_bottiglina, btn_plus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_minus_bottiglina, btn_minus_cb, LV_EVENT_CLICKED, NULL);

    // Label bottiglina sotto lo slider
    label_bottiglina_erogazione = lv_label_create(schermo_impostazioni_erogazione);
    lv_obj_align_to(label_bottiglina_erogazione, slider_bottiglina_erogazione, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    {
      static char buf[32];
      snprintf(buf, sizeof(buf), "Bottiglina: %d s", tempoContenitore[1]);
      lv_label_set_text(label_bottiglina_erogazione, buf);
    }

    // SLIDER BOTTIGLIA con bottoni agli estremi
    slider_bottiglia_erogazione = lv_slider_create(schermo_impostazioni_erogazione);
    lv_slider_set_range(slider_bottiglia_erogazione, 0, 60);
    lv_slider_set_value(slider_bottiglia_erogazione, tempoContenitore[2], LV_ANIM_OFF);
    lv_obj_set_width(slider_bottiglia_erogazione, 250);
    lv_obj_set_height(slider_bottiglia_erogazione, 25);
    lv_obj_align(slider_bottiglia_erogazione, LV_ALIGN_CENTER, 0, 80);
    lv_obj_set_style_bg_color(slider_bottiglia_erogazione, lv_color_hex(0x0F73B5), LV_PART_INDICATOR);
    lv_obj_add_event_cb(slider_bottiglia_erogazione, slider_event_erogazione_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Bottoni + e - per bottiglia agli estremi
    lv_obj_t *btn_plus_bottiglia, *btn_minus_bottiglia;
    crea_bottoni_plus_minus_estremi(schermo_impostazioni_erogazione, slider_bottiglia_erogazione, &btn_plus_bottiglia, &btn_minus_bottiglia);
    lv_obj_add_event_cb(btn_plus_bottiglia, btn_plus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_minus_bottiglia, btn_minus_cb, LV_EVENT_CLICKED, NULL);

    // Label bottiglia sotto lo slider
    label_bottiglia_erogazione = lv_label_create(schermo_impostazioni_erogazione);
    lv_obj_align_to(label_bottiglia_erogazione, slider_bottiglia_erogazione, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    {
      static char buf[32];
      snprintf(buf, sizeof(buf), "Bottiglia: %d s", tempoContenitore[2]);
      lv_label_set_text(label_bottiglia_erogazione, buf);
    }

    // Bottone Salva in basso
    lv_obj_t *btn_salva = lv_btn_create(schermo_impostazioni_erogazione);
    lv_obj_set_size(btn_salva, 120, 50);
    lv_obj_align(btn_salva, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_t *label_btn = lv_label_create(btn_salva);
    lv_label_set_text(label_btn, "Salva");
    lv_obj_center(label_btn);
    lv_obj_set_style_bg_color(btn_salva, lv_color_hex(0x0F73B5), 0);
    lv_obj_add_event_cb(
      btn_salva, [](lv_event_t *e) {
        safe_lvgl_operation([&]() {
          lv_scr_load_anim(schermo2, LV_SCR_LOAD_ANIM_FADE_IN, 150, 0, false);
        });
      },
      LV_EVENT_CLICKED, NULL);

    lv_scr_load(schermo_impostazioni_erogazione);
  });
}

void callback_bottone_impostazioni_allarme(lv_event_t *evento) {
  safe_lvgl_operation([&]() {
    safe_screen_delete(schermo_allarmi);

    // Usa layout standard
    schermo_allarmi = crea_schermata_impostazioni_standard("ALLARMI FILTRO");

    // Stato attuale allarme (in alto)
    label_stato_allarme = lv_label_create(schermo_allarmi);
    lv_obj_align(label_stato_allarme, LV_ALIGN_CENTER, 0, -100);
    lv_obj_set_style_text_font(label_stato_allarme, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label_stato_allarme, LV_TEXT_ALIGN_CENTER, 0);

    // Switch attivazione allarmi
    lv_obj_t *label_switch = lv_label_create(schermo_allarmi);
    lv_label_set_text(label_switch, "Allarmi attivi:");
    lv_obj_align(label_switch, LV_ALIGN_CENTER, -70, -30);

    switch_allarmi_attivi = lv_switch_create(schermo_allarmi);
    lv_obj_align(switch_allarmi_attivi, LV_ALIGN_CENTER, 70, -30);
    lv_obj_set_style_bg_color(switch_allarmi_attivi, lv_color_hex(0x0F73B5), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(switch_allarmi_attivi, switch_allarmi_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Slider giorni preavviso
    lv_obj_t *label_preavviso = lv_label_create(schermo_allarmi);
    lv_label_set_text(label_preavviso, "Giorni preavviso (1-30):");
    lv_obj_align(label_preavviso, LV_ALIGN_CENTER, 0, 5);

    slider_giorni_preavviso = lv_slider_create(schermo_allarmi);
    lv_slider_set_range(slider_giorni_preavviso, 1, 30);
    lv_obj_set_width(slider_giorni_preavviso, 250);
    lv_obj_set_height(slider_giorni_preavviso, 25);
    lv_obj_align(slider_giorni_preavviso, LV_ALIGN_CENTER, 0, 35);
    lv_obj_set_style_bg_color(slider_giorni_preavviso, lv_color_hex(0x0F73B5), LV_PART_INDICATOR);
    lv_obj_add_event_cb(slider_giorni_preavviso, slider_preavviso_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Bottoni + e - per preavviso
    lv_obj_t *btn_plus_preavviso, *btn_minus_preavviso;
    crea_bottoni_plus_minus_estremi(schermo_allarmi, slider_giorni_preavviso, &btn_plus_preavviso, &btn_minus_preavviso);
    lv_obj_add_event_cb(btn_plus_preavviso, btn_plus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_minus_preavviso, btn_minus_cb, LV_EVENT_CLICKED, NULL);

    label_giorni_preavviso = lv_label_create(schermo_allarmi);
    lv_obj_align_to(label_giorni_preavviso, slider_giorni_preavviso, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    // Slider giorni critico
    lv_obj_t *label_critico = lv_label_create(schermo_allarmi);
    lv_label_set_text(label_critico, "Giorni allarme critico (1-15):");
    lv_obj_align(label_critico, LV_ALIGN_CENTER, 0, 75);

    slider_giorni_critico = lv_slider_create(schermo_allarmi);
    lv_slider_set_range(slider_giorni_critico, 1, 15);
    lv_obj_set_width(slider_giorni_critico, 250);
    lv_obj_set_height(slider_giorni_critico, 25);
    lv_obj_align(slider_giorni_critico, LV_ALIGN_CENTER, 0, 105);
    lv_obj_set_style_bg_color(slider_giorni_critico, lv_color_hex(0x0F73B5), LV_PART_INDICATOR);
    lv_obj_add_event_cb(slider_giorni_critico, slider_critico_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Bottoni + e - per critico
    lv_obj_t *btn_plus_critico, *btn_minus_critico;
    crea_bottoni_plus_minus_estremi(schermo_allarmi, slider_giorni_critico, &btn_plus_critico, &btn_minus_critico);
    lv_obj_add_event_cb(btn_plus_critico, btn_plus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_minus_critico, btn_minus_cb, LV_EVENT_CLICKED, NULL);

    label_giorni_critico = lv_label_create(schermo_allarmi);
    lv_obj_align_to(label_giorni_critico, slider_giorni_critico, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    // Dropdown frequenza notifiche
    lv_obj_t *label_freq = lv_label_create(schermo_allarmi);
    lv_label_set_text(label_freq, "Frequenza notifiche:");
    lv_obj_align(label_freq, LV_ALIGN_CENTER, -70, 145);

    dropdown_frequenza = lv_dropdown_create(schermo_allarmi);
    lv_dropdown_set_options(dropdown_frequenza, "Ogni avvio\nUna volta al giorno\nUna volta a settimana");
    lv_obj_set_width(dropdown_frequenza, 200);
    lv_obj_align(dropdown_frequenza, LV_ALIGN_CENTER, 50, 145);
    lv_obj_add_event_cb(dropdown_frequenza, dropdown_frequenza_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Bottoni azione
    lv_obj_t *btn_configura_filtro = lv_btn_create(schermo_allarmi);
    lv_obj_set_size(btn_configura_filtro, 140, 50);
    lv_obj_align(btn_configura_filtro, LV_ALIGN_BOTTOM_LEFT, 40, -30);
    lv_obj_t *label_configura = lv_label_create(btn_configura_filtro);
    lv_label_set_text(label_configura, "Installa\nFiltro");
    lv_obj_center(label_configura);
    lv_obj_set_style_bg_color(btn_configura_filtro, lv_color_hex(0x00AA00), 0);

    lv_obj_add_event_cb(
      btn_configura_filtro, [](lv_event_t *e) {
        reset_filtro();              // Installa nuovo filtro
        aggiorna_display_allarmi();  // Aggiorna la schermata

        // Mostra conferma
        safe_lvgl_operation([&]() {
          lv_obj_t *success_box = lv_msgbox_create(NULL, "Filtro Installato",
                                                   "✅ Nuovo filtro installato!\n"
                                                   "Data installazione: OGGI",
                                                   NULL, true);
          lv_obj_center(success_box);
        });
      },
      LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_snooze_clear = lv_btn_create(schermo_allarmi);
    lv_obj_set_size(btn_snooze_clear, 120, 50);
    lv_obj_align(btn_snooze_clear, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_t *label_snooze = lv_label_create(btn_snooze_clear);
    lv_label_set_text(label_snooze, "Rimuovi\nSnooze");
    lv_obj_center(label_snooze);
    lv_obj_set_style_bg_color(btn_snooze_clear, lv_color_hex(0x888888), 0);

    lv_obj_add_event_cb(
      btn_snooze_clear, [](lv_event_t *e) {
        clear_snooze();
        aggiorna_display_allarmi();
      },
      LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_salva = lv_btn_create(schermo_allarmi);
    lv_obj_set_size(btn_salva, 100, 50);
    lv_obj_align(btn_salva, LV_ALIGN_BOTTOM_RIGHT, -40, -30);
    lv_obj_t *label_salva = lv_label_create(btn_salva);
    lv_label_set_text(label_salva, "Salva");
    lv_obj_center(label_salva);
    lv_obj_set_style_bg_color(btn_salva, lv_color_hex(0x0F73B5), 0);

    lv_obj_add_event_cb(
      btn_salva, [](lv_event_t *e) {
        salva_impostazioni_allarmi();
        safe_lvgl_operation([&]() {
          lv_scr_load_anim(schermo2, LV_SCR_LOAD_ANIM_FADE_IN, 150, 0, false);
        });
      },
      LV_EVENT_CLICKED, NULL);

    lv_scr_load(schermo_allarmi);
  });

  aggiorna_display_allarmi();
}

// ============================================
// CONTROLLO PERIODICO FILTRO (THREAD-SAFE)
// ============================================

void controlla_stato_filtro() {
  static unsigned long ultimo_controllo = 0;
  static bool primo_controllo = true;
  static bool sistema_inizializzato = false;

  if (!sistema_inizializzato) {
    if (millis() < 10000) return;
    sistema_inizializzato = true;
  }

  // Controllo meno frequente: ogni 30 minuti invece di ogni ora
  if (primo_controllo || (millis() - ultimo_controllo > 1800000)) {
    ultimo_controllo = millis();
    primo_controllo = false;

    if (!controllo_filtro_attivo || giorni_installazione_filtro == 0) {
      return;
    }

    bool scaduto = filtro_scaduto();
    int giorni = calcola_giorni_dall_installazione();
    int giorni_rimanenti = (limite_mesi_filtro * 30) - giorni;

    if (scaduto && !filtro_scaduto_notificato) {
      mostra_notifica_filtro_scaduto();
      return;
    }

    static int ultimo_giorno_promemoria = -1;
    if (!scaduto && giorni_rimanenti <= 15 && giorni_rimanenti > 0) {
      if (giorni != ultimo_giorno_promemoria) {
        ultimo_giorno_promemoria = giorni;
        mostra_notifica_filtro_prossimo_scadenza();
      }
    }
  }
}

// ============================================
// FUNZIONE INIZIALIZZAZIONE SD (THREAD-SAFE)
// ============================================
bool initSDCardWithExistingI2C() {
  Serial.println("=== INIZIALIZZAZIONE SD CON IO EXPANDER (I2C ESISTENTE) ===");

  // Crea mutex I2C se non esiste
  if (i2c_mutex == NULL) {
    i2c_mutex = xSemaphoreCreateMutex();
    if (i2c_mutex == NULL) {
      Serial.println("❌ Errore creazione mutex I2C");
      return false;
    }
  }

  esp_err_t ret = ESP_OK;

  Serial.println("✅ Usando I2C già inizializzato dal sistema");

  ret = esp_io_expander_new_i2c_ch422g(I2C_NUM_0, EXAMPLE_I2C_ADDR, &io_expander);
  if (ret != ESP_OK || io_expander == NULL) {
    Serial.printf("❌ Errore inizializzazione IO expander: %s\n", esp_err_to_name(ret));
    return false;
  }

  Serial.println("✅ IO Expander inizializzato su I2C esistente");

  uint32_t output_pins = BIT(TP_RST) | BIT(LCD_BL) | BIT(LCD_RST) | BIT(SD_CS) | BIT(USB_SEL);

  bool config_success = safe_i2c_operation([&]() -> esp_err_t {
    esp_err_t result = esp_io_expander_set_dir(io_expander, output_pins, IO_EXPANDER_OUTPUT);
    if (result != ESP_OK) return result;

    esp_io_expander_set_level(io_expander, BIT(TP_RST) | BIT(LCD_BL) | BIT(LCD_RST), 1);
    esp_io_expander_set_level(io_expander, BIT(USB_SEL), 0);
    esp_io_expander_set_level(io_expander, BIT(SD_CS), 1);

    return ESP_OK;
  });

  if (!config_success) {
    Serial.println("❌ Errore configurazione pin IO Expander");
    return false;
  }

  Serial.println("✅ Pin IO Expander configurati");

  // NUOVO: Prima terminaziamo tutte le connessioni SD esistenti
  SD.end();
  delay(100);

  SPI.setHwCs(false);
  SPI.begin(SD_CLK, SD_MISO, SD_MOSI, -1);
  Serial.println("✅ SPI inizializzato");

  // STRATEGIA MULTIPLA: Diverse frequenze e approcci
  uint32_t frequencies[] = { 400000, 1000000, 4000000, 8000000 };

  for (int strategy = 0; strategy < 3; strategy++) {
    Serial.printf("\n🔄 STRATEGIA %d:\n", strategy + 1);

    for (int i = 0; i < 4; i++) {
      Serial.printf("   Tentativo freq: %lu Hz\n", frequencies[i]);

      // Strategia 1: Con gestione IO expander (originale)
      if (strategy == 0) {
        bool sd_init_success = safe_i2c_operation([&]() -> esp_err_t {
          esp_io_expander_set_level(io_expander, BIT(SD_CS), 0);
          delay(50);  // Aumentato da 10ms a 50ms

          bool result = SD.begin(-1, SPI, frequencies[i]);

          delay(50);
          esp_io_expander_set_level(io_expander, BIT(SD_CS), 1);

          if (result) {
            uint8_t cardType = SD.cardType();
            uint64_t totalBytes = SD.totalBytes();

            Serial.printf("   ✅ Card Type: %d, TotalBytes: %llu\n", cardType, totalBytes);

            if (cardType != CARD_NONE && totalBytes > 0) {
              Serial.printf("✅ SD OK! Freq: %lu Hz, TotalBytes: %lluMB\n",
                            frequencies[i], totalBytes / (1024 * 1024));
              return ESP_OK;
            } else {
              Serial.printf("   ⚠️ Card rilevata ma TotalBytes = %llu\n", totalBytes);
              SD.end();
              delay(100);
            }
          }
          return ESP_FAIL;
        });

        if (sd_init_success) return true;
      }

      // Strategia 2: Senza gestione CS (SD gestisce CS automaticamente)
      else if (strategy == 1) {
        Serial.println("   [Senza gestione CS manuale]");

        if (SD.begin(-1, SPI, frequencies[i])) {
          uint8_t cardType = SD.cardType();
          uint64_t totalBytes = SD.totalBytes();

          Serial.printf("   ✅ Card Type: %d, TotalBytes: %llu\n", cardType, totalBytes);

          if (cardType != CARD_NONE && totalBytes > 0) {
            Serial.printf("✅ SD OK! Freq: %lu Hz, TotalBytes: %lluMB\n",
                          frequencies[i], totalBytes / (1024 * 1024));
            return true;
          } else {
            Serial.printf("   ⚠️ Card rilevata ma TotalBytes = %llu\n", totalBytes);
            SD.end();
            delay(100);
          }
        }
      }

      // Strategia 3: CS fisso LOW durante l'inizializzazione
      else if (strategy == 2) {
        Serial.println("   [CS fisso LOW]");

        safe_i2c_operation([&]() -> esp_err_t {
          esp_io_expander_set_level(io_expander, BIT(SD_CS), 0);
          return ESP_OK;
        });

        delay(100);

        if (SD.begin(-1, SPI, frequencies[i])) {
          uint8_t cardType = SD.cardType();
          uint64_t totalBytes = SD.totalBytes();

          Serial.printf("   ✅ Card Type: %d, TotalBytes: %llu\n", cardType, totalBytes);

          if (cardType != CARD_NONE && totalBytes > 0) {
            // NON rimettere CS a HIGH - lascia CS fisso a LOW per mantenere il mount!
            Serial.printf("✅ SD OK! Freq: %lu Hz, TotalBytes: %lluMB\n",
                          frequencies[i], totalBytes / (1024 * 1024));
            Serial.println("🔧 CS mantenuto LOW per preservare mount file system");

            // Imposta flag per SDCardManager
            SDCardManager::setCSFixedLow(true);

            return true;
          } else {
            Serial.printf("   ⚠️ Card rilevata ma TotalBytes = %llu\n", totalBytes);
            SD.end();
            delay(100);
          }
        }

        // Solo in caso di fallimento rimetti CS a HIGH
        safe_i2c_operation([&]() -> esp_err_t {
          esp_io_expander_set_level(io_expander, BIT(SD_CS), 1);
          return ESP_OK;
        });
      }

      delay(200);  // Pausa tra tentativi
    }

    Serial.printf("❌ Strategia %d fallita\n", strategy + 1);
    SD.end();
    delay(500);
  }

  Serial.println("❌ Tutte le strategie SD fallite");
  return false;
}

void deseleziona_bottoni();
void evidenzia_bottone(lv_obj_t *bersaglio);
void aggiorna_debug_label(uint8_t *valore);
static void gesture_cb(lv_event_t *e);
lv_obj_t *crea_schermata_generica(const char *titolo);
void cancella_arco();
void aggiorna_orologio();
void setup_rtc();
static void salva_orario_cb(lv_event_t *e);
void aggiorna_label_orario();
void mostra_errore_sd(const char *msg);

// ============================================
// FUNZIONE INIZIALIZZAZIONE SPIFFS MIGLIORATA
// ============================================
bool inizializza_spiffs() {
  Serial.println("=== INIZIALIZZAZIONE SPIFFS ===");

  if (!SPIFFS.begin(true)) {  // true = format se necessario
    Serial.println("❌ Errore inizializzazione SPIFFS");
    spiffs_disponibile = false;
    return false;
  }

  Serial.println("✅ SPIFFS inizializzato");

  // Mostra info SPIFFS
  size_t total_bytes = SPIFFS.totalBytes();
  size_t used_bytes = SPIFFS.usedBytes();
  size_t free_bytes = total_bytes - used_bytes;

  Serial.printf("SPIFFS: %d total, %d usati, %d liberi\n",
                total_bytes, used_bytes, free_bytes);

  // Verifica se logo esiste
  if (SPIFFS.exists(LOGO_SPIFFS_PATH)) {
    File logoFile = SPIFFS.open(LOGO_SPIFFS_PATH, "r");
    if (logoFile) {
      Serial.printf("✅ Logo trovato: %d bytes\n", logoFile.size());
      logoFile.close();
    }
  } else {
    Serial.println("⚠️ Logo non trovato in SPIFFS");
  }

  // Lista tutti i file
  Serial.println("File in SPIFFS:");
  File root = SPIFFS.open("/");
  if (root) {
    File file = root.openNextFile();
    while (file) {
      Serial.printf("- %s (%d bytes)\n", file.name(), file.size());
      file = root.openNextFile();
    }
    root.close();
  }

  spiffs_disponibile = true;
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("=== ESP32-S3-Touch-LCD-5 Waveshare - Avvio ===");
  Serial.println("🔄 VERSIONE MODIFICATA: Valvole via SERIALE (no I2C)");

  // ============================================
  // INIZIALIZZAZIONE SISTEMA ANTI-FREEZE
  // ============================================

  // Configura WDT con la nuova API ESP-IDF v5.x
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT_SECONDS * 1000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,  // monitor all cores
    .trigger_panic = true,
  };
  esp_err_t wdt_result = esp_task_wdt_init(&wdt_config);
  if (wdt_result == ESP_OK) {
    // Aggiungi il task corrente al WDT
    esp_err_t add_result = esp_task_wdt_add(NULL);
    if (add_result == ESP_OK) {
      wdt_initialized = true;
      Serial.println("✅ Watchdog timer inizializzato e task aggiunto");
    } else {
      Serial.printf("⚠️ Errore aggiunta task al WDT: %s\n", esp_err_to_name(add_result));
    }
  } else {
    Serial.printf("⚠️ Errore inizializzazione WDT: %s\n", esp_err_to_name(wdt_result));
  }

  // Inizializza monitor sistema
  monitor.init();

  Serial.println("✅ Sistema anti-freeze inizializzato");

  Serial.println("Initializing board");
  Board *board = new Board();
  board->init();

  // Inizializza SPIFFS PRIMA delle valvole e LVGL
  inizializza_spiffs();

#if LVGL_PORT_AVOID_TEARING_MODE
  auto lcd = board->getLCD();
  lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
  auto lcd_bus = lcd->getBus();
  if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
    static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
  }
#endif
#endif
  assert(board->begin());

  // ============================================
  // INIZIALIZZA VALVOLE VIA SERIALE (SOSTITUISCE I2C)
  // ============================================
  Serial.println("🔄 Inizializzazione valvole SERIALE...");
  inizializza_valvole();

  Serial.println("Initializing LVGL");
  lvgl_port_init(board->getLCD(), board->getTouch());

  Serial.println("Creating UI");

  // ============================================
  // CREAZIONE UI THREAD-SAFE CON MODIFICHE ESTETICHE
  // ============================================

  if (!safe_lvgl_operation([&]() {
        schermo1 = lv_obj_create(NULL);
        schermo2 = lv_obj_create(NULL);

        lv_obj_add_event_cb(schermo1, gesture_cb, LV_EVENT_GESTURE, NULL);
        lv_obj_add_event_cb(schermo2, gesture_cb, LV_EVENT_GESTURE, NULL);

        // ============================================
        // SCHERMO 1 - PAGINA PRINCIPALE (FLOW)
        // ============================================

        // CREAZIONE CERCHIO DI SELEZIONE - DIMENSIONE CORRETTA (SIZE_ICON_BIG)
        cerchio_selezione = lv_obj_create(schermo1);
        lv_obj_set_size(cerchio_selezione, SIZE_ICON_BIG - 20, SIZE_ICON_BIG - 20);
        lv_obj_set_style_radius(cerchio_selezione, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(cerchio_selezione, lv_color_hex(0x0F73B5), 0);
        lv_obj_set_style_bg_opa(cerchio_selezione, LV_OPA_50, 0);
        lv_obj_clear_flag(cerchio_selezione, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_move_background(cerchio_selezione);

        // creazione label orologio
        label_orologio = lv_label_create(schermo1);
        lv_obj_align(label_orologio, LV_ALIGN_TOP_RIGHT, -25, 50);
        lv_obj_set_style_text_font(label_orologio, &lv_font_montserrat_30, 0);
        lv_label_set_text(label_orologio, "00:00:00");

        // Creazione label "FLOW" al centro
        lv_obj_t *label_flow = lv_label_create(schermo1);
        lv_label_set_text(label_flow, "FLOW");
        lv_obj_align(label_flow, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_set_style_text_font(label_flow, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(label_flow, lv_color_hex(0x0F73B5), 0);

        // Creazione linea separatrice SCHERMO1
        lv_obj_t *linea_separatrice_schermo1 = lv_obj_create(schermo1);
        lv_obj_set_size(linea_separatrice_schermo1, MAX_LARGHEZZA - 40, 3);
        lv_obj_align(linea_separatrice_schermo1, LV_ALIGN_TOP_MID, 0, 150);
        lv_obj_set_style_bg_color(linea_separatrice_schermo1, lv_color_hex(0x0F73B5), 0);
        lv_obj_set_style_border_width(linea_separatrice_schermo1, 0, 0);
        lv_obj_set_style_radius(linea_separatrice_schermo1, 0, 0);
        lv_obj_clear_flag(linea_separatrice_schermo1, LV_OBJ_FLAG_CLICKABLE);

        // ============================================
        // SCHERMO 2 - PAGINA IMPOSTAZIONI (NUOVO)
        // ============================================

        // Creazione label "IMPOSTAZIONI" al centro (stessa posizione di FLOW)
        lv_obj_t *label_impostazioni = lv_label_create(schermo2);
        lv_label_set_text(label_impostazioni, "IMPOSTAZIONI");
        lv_obj_align(label_impostazioni, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_set_style_text_font(label_impostazioni, &lv_font_montserrat_48, 0);   // Stesso font di FLOW
        lv_obj_set_style_text_color(label_impostazioni, lv_color_hex(0x0F73B5), 0);  // Stesso colore di FLOW

        // Creazione linea separatrice SCHERMO2 (stessa della schermata principale)
        lv_obj_t *linea_separatrice_schermo2 = lv_obj_create(schermo2);
        lv_obj_set_size(linea_separatrice_schermo2, MAX_LARGHEZZA - 40, 3);                // Stessa larghezza
        lv_obj_align(linea_separatrice_schermo2, LV_ALIGN_TOP_MID, 0, 150);                // Stessa posizione Y
        lv_obj_set_style_bg_color(linea_separatrice_schermo2, lv_color_hex(0x0F73B5), 0);  // Stesso colore
        lv_obj_set_style_border_width(linea_separatrice_schermo2, 0, 0);
        lv_obj_set_style_radius(linea_separatrice_schermo2, 0, 0);
        lv_obj_clear_flag(linea_separatrice_schermo2, LV_OBJ_FLAG_CLICKABLE);

    // ============================================
    // RESTO DEL CODICE (bottoni ecc.)
    // ============================================

#define PRIMA_RIGA 180    // Aumentato per fare spazio alla linea
#define SECONDA_RIGA 340  // Aumentato proporzionalmente
#define PRIMA_COLONNA 120
#define SECONDA_COLONNA 340
#define TERZA_COLONNA 560

        // creazione bottoni acqua (SCHERMO1)
        bottone_acqua_liscia = lv_imgbtn_create(schermo1);
        lv_imgbtn_set_src(bottone_acqua_liscia, LV_IMGBTN_STATE_RELEASED, NULL, &acqua_liscia, NULL);
        lv_obj_set_size(bottone_acqua_liscia, SIZE_ICON_BIG, SIZE_ICON_BIG);
        lv_obj_align(bottone_acqua_liscia, LV_ALIGN_TOP_LEFT, PRIMA_COLONNA, PRIMA_RIGA);

        bottone_acqua_fresca = lv_imgbtn_create(schermo1);
        lv_imgbtn_set_src(bottone_acqua_fresca, LV_IMGBTN_STATE_RELEASED, NULL, &acqua_fresca, NULL);
        lv_obj_set_size(bottone_acqua_fresca, SIZE_ICON_BIG, SIZE_ICON_BIG);
        lv_obj_align(bottone_acqua_fresca, LV_ALIGN_TOP_LEFT, SECONDA_COLONNA, PRIMA_RIGA);

        bottone_acqua_frizzante = lv_imgbtn_create(schermo1);
        lv_imgbtn_set_src(bottone_acqua_frizzante, LV_IMGBTN_STATE_RELEASED, NULL, &acqua_frizzante, NULL);
        lv_obj_set_size(bottone_acqua_frizzante, SIZE_ICON_BIG, SIZE_ICON_BIG);
        lv_obj_align(bottone_acqua_frizzante, LV_ALIGN_TOP_LEFT, TERZA_COLONNA, PRIMA_RIGA);

        // bottoni contenitori (SCHERMO1)
        bottone_bicchiere = lv_imgbtn_create(schermo1);
        lv_imgbtn_set_src(bottone_bicchiere, LV_IMGBTN_STATE_RELEASED, 0, &bicchiere, 0);
        lv_obj_set_size(bottone_bicchiere, SIZE_ICON_BIG, SIZE_ICON_BIG);
        lv_obj_align(bottone_bicchiere, LV_ALIGN_TOP_LEFT, PRIMA_COLONNA, SECONDA_RIGA);

        bottone_bottiglina = lv_imgbtn_create(schermo1);
        lv_imgbtn_set_src(bottone_bottiglina, LV_IMGBTN_STATE_RELEASED, 0, &bottiglina, 0);
        lv_obj_set_size(bottone_bottiglina, SIZE_ICON_BIG, SIZE_ICON_BIG);
        lv_obj_align(bottone_bottiglina, LV_ALIGN_TOP_LEFT, SECONDA_COLONNA, SECONDA_RIGA);

        bottone_bottiglia = lv_imgbtn_create(schermo1);
        lv_imgbtn_set_src(bottone_bottiglia, LV_IMGBTN_STATE_RELEASED, 0, &bottiglia, 0);
        lv_obj_set_size(bottone_bottiglia, SIZE_ICON_BIG, SIZE_ICON_BIG);
        lv_obj_align(bottone_bottiglia, LV_ALIGN_TOP_LEFT, TERZA_COLONNA, SECONDA_RIGA);

        // bottoni impostazioni schermo2 - PRIMA RIGA (3 bottoni)
        bottone_impostazioni_schermo = lv_imgbtn_create(schermo2);
        lv_imgbtn_set_src(bottone_impostazioni_schermo, LV_IMGBTN_STATE_RELEASED, 0, &impostazioni_schermo, 0);
        lv_obj_set_size(bottone_impostazioni_schermo, SIZE_ICON_BIG, SIZE_ICON_BIG);
        lv_obj_align(bottone_impostazioni_schermo, LV_ALIGN_TOP_LEFT, PRIMA_COLONNA, PRIMA_RIGA);

        bottone_impostazioni_bicchiere = lv_imgbtn_create(schermo2);
        lv_imgbtn_set_src(bottone_impostazioni_bicchiere, LV_IMGBTN_STATE_RELEASED, 0, &bicchiere, 0);
        lv_obj_set_size(bottone_impostazioni_bicchiere, SIZE_ICON_BIG, SIZE_ICON_BIG);
        lv_obj_align(bottone_impostazioni_bicchiere, LV_ALIGN_TOP_LEFT, SECONDA_COLONNA, PRIMA_RIGA);

        bottone_impostazioni_contatore = lv_imgbtn_create(schermo2);
        lv_imgbtn_set_src(bottone_impostazioni_contatore, LV_IMGBTN_STATE_RELEASED, 0, &impostazioni_contatore, 0);
        lv_obj_set_size(bottone_impostazioni_contatore, SIZE_ICON_BIG, SIZE_ICON_BIG);
        lv_obj_align(bottone_impostazioni_contatore, LV_ALIGN_TOP_LEFT, TERZA_COLONNA, PRIMA_RIGA);

        // bottoni impostazioni seconda riga (1 bottone centrato)
        bottone_impostazioni_allarme = lv_imgbtn_create(schermo2);
        lv_imgbtn_set_src(bottone_impostazioni_allarme, LV_IMGBTN_STATE_RELEASED, 0, &impostazioni_allarme, 0);
        lv_obj_set_size(bottone_impostazioni_allarme, SIZE_ICON_BIG, SIZE_ICON_BIG);
        lv_obj_align(bottone_impostazioni_allarme, LV_ALIGN_TOP_LEFT, SECONDA_COLONNA, SECONDA_RIGA);
      })) {
    Serial.println("❌ Errore creazione UI base!");
    ESP.restart();
  }

  // Collegamento callback
  lv_obj_add_event_cb(bottone_acqua_liscia, callback_bottone_acqua_liscia, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_acqua_fresca, callback_bottone_acqua_fresca, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_acqua_frizzante, callback_bottone_acqua_frizzante, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_bicchiere, callback_bottone_bicchiere, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_bottiglina, callback_bottone_bottiglina, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_bottiglia, callback_bottone_bottiglia, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_impostazioni_schermo, callback_bottone_impostazioni_schermo, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_impostazioni_bicchiere, callback_bottone_impostazioni_erogazione, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_impostazioni_contatore, callback_bottone_impostazioni_contatore, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_impostazioni_allarme, callback_bottone_impostazioni_allarme, LV_EVENT_CLICKED, NULL);

  // Inizializza schermo
  safe_lvgl_operation([&]() {
    lv_scr_load(schermo1);
    evidenzia_bottone(bottone_acqua_liscia);
  });
  tipoAcqua = 0;

  Serial.println("🎉 UI completata!");

  // INIZIALIZZAZIONE SISTEMI
  carica_dati_contatore();
  Serial.println("✅ Sistema contatore inizializzato");

  // Inizializzazione SD
  Serial.println("Inizializzazione SD card con IO expander (I2C esistente)...");
  sd_disponibile = initSDCardWithExistingI2C();

  if (sd_disponibile) {
    Serial.println("🎉 SD card inizializzata!");

    Serial.printf("Card Type: %d\n", SD.cardType());
    Serial.printf("Card Size: %lluMB\n", SD.cardSize() / (1024 * 1024));
    Serial.printf("Total Bytes: %lluMB\n", SD.totalBytes() / (1024 * 1024));
    Serial.printf("Used Bytes: %lluMB\n", SD.usedBytes() / (1024 * 1024));

  } else {
    Serial.println("⚠️ SD card non disponibile - UI funziona comunque");
  }

  // INIZIALIZZAZIONE SISTEMA LOGO (NUOVA IMPLEMENTAZIONE)
  Serial.println("Inizializzazione sistema logo...");
  delay(500);  // Pausa per stabilizzare LVGL
  setup_logo_system();

  // Stampa statistiche memoria
  monitor.print_stats();

  Serial.println("🎉 Setup completato! Sistema pronto.");
  Serial.println("📡 Valvole ora comunicano via RS485 - zero traffico I2C per le valvole!");
  Serial.printf("🔌 RS485 integrato: %s, %d baud, RX=GPIO%d, TX=GPIO%d\n",
                USE_MAIN_SERIAL ? "Serial" : "Serial2",
                VALVE_SERIAL_BAUDRATE, VALVE_SERIAL_RX_PIN, VALVE_SERIAL_TX_PIN);
}

void aggiorna_debug_label(uint8_t valore) {
  static char buffer[32];
  snprintf(buffer, sizeof(buffer), "Valore: %u", valore);
  safe_lvgl_operation([&]() {
    lv_label_set_text(debug_label, buffer);
  });
}

void evidenzia_bottone(lv_obj_t *bersaglio) {
  safe_lvgl_operation([&]() {
    lv_obj_align_to(cerchio_selezione, bersaglio, LV_ALIGN_CENTER, 0, 0);
  });
}

// Variabile globale temporanea per il tipo di contenitore
static uint8_t tipo_contenitore_corrente = 0;

// Callback per fine animazione (thread-safe)
static void animazione_completata_cb(lv_anim_t *a) {
  safe_lvgl_operation([&]() {
    if (arco_timer) {
      lv_obj_del(arco_timer);
      arco_timer = NULL;
    }
  });

  ferma_erogazione_valvole();
  lockErogazione = 0;
  aggiorna_contatore(tipo_contenitore_corrente);

  lv_obj_add_event_cb(schermo1, gesture_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(schermo2, gesture_cb, LV_EVENT_GESTURE, NULL);
}

void erogazione(uint8_t tempo, lv_obj_t *target, uint8_t tipo_contenitore) {
  avvia_erogazione_valvole(tipoAcqua);

  if (!safe_lvgl_operation([&]() {
        lockErogazione = 1;
        tipo_contenitore_corrente = tipo_contenitore;

        if (arco_timer) {
          lv_obj_del(arco_timer);
          arco_timer = NULL;
        }

        arco_timer = lv_arc_create(schermo1);
        lv_obj_set_size(arco_timer, SIZE_ICON_BIG + 24, SIZE_ICON_BIG + 24);
        lv_obj_align_to(arco_timer, target, LV_ALIGN_CENTER, 0, 0);

        lv_arc_set_range(arco_timer, 0, 360);
        lv_arc_set_rotation(arco_timer, 270);
        lv_arc_set_bg_angles(arco_timer, 0, 360);
        lv_arc_set_value(arco_timer, 0);
        lv_arc_set_mode(arco_timer, LV_ARC_MODE_NORMAL);

        lv_obj_remove_style(arco_timer, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(arco_timer, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_color(arco_timer, lv_color_hex(0x0F73B5), LV_PART_INDICATOR);  // NUOVO COLORE BLU
        lv_obj_set_style_arc_width(arco_timer, 8, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arco_timer, LV_OPA_COVER, LV_PART_MAIN);

        lv_obj_remove_event_cb(schermo1, gesture_cb);
        lv_obj_remove_event_cb(schermo2, gesture_cb);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, arco_timer);
        lv_anim_set_values(&a, 0, 360);
        lv_anim_set_time(&a, tempo * 1000);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_arc_set_value);
        lv_anim_set_path_cb(&a, lv_anim_path_linear);
        lv_anim_set_ready_cb(&a, animazione_completata_cb);
        lv_anim_start(&a);
      })) {
    Serial.println("❌ Errore avvio erogazione!");
    ferma_erogazione_valvole();
    lockErogazione = 0;
  }
}

static void gesture_cb(lv_event_t *e) {
  lv_obj_t *target = lv_event_get_target(e);
  lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

  if (dir == LV_DIR_RIGHT) {
    safe_lvgl_operation([&]() {
      lv_scr_load_anim(schermo2, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 150, 0, false);
    });
  } else if (dir == LV_DIR_LEFT) {
    safe_lvgl_operation([&]() {
      lv_scr_load_anim(schermo1, LV_SCR_LOAD_ANIM_MOVE_LEFT, 150, 0, false);
    });
  }
}

void callback_bottone_acqua_liscia(lv_event_t *evento) {
  if (!lockErogazione) {
    evidenzia_bottone(bottone_acqua_liscia);
    tipoAcqua = 0;
  }
}

void callback_bottone_acqua_fresca(lv_event_t *evento) {
  if (!lockErogazione) {
    evidenzia_bottone(bottone_acqua_fresca);
    tipoAcqua = 1;
  }
}

void callback_bottone_acqua_frizzante(lv_event_t *evento) {
  if (!lockErogazione) {
    evidenzia_bottone(bottone_acqua_frizzante);
    tipoAcqua = 2;
  }
}

void callback_bottone_bicchiere(lv_event_t *evento) {
  if (!lockErogazione) {
    erogazione(tempoContenitore[0], bottone_bicchiere, 0);
  } else
    cancella_arco();
}

void callback_bottone_bottiglina(lv_event_t *evento) {
  if (!lockErogazione) {
    erogazione(tempoContenitore[1], bottone_bottiglina, 1);
  } else
    cancella_arco();
}

void callback_bottone_bottiglia(lv_event_t *evento) {
  if (!lockErogazione) {
    erogazione(tempoContenitore[2], bottone_bottiglia, 2);
  } else
    cancella_arco();
}

void callback_bottone_impostazioni_schermo(lv_event_t *evento) {
  safe_lvgl_operation([&]() {
    // Usa la nuova funzione helper
    schermo_impostazioni_orologio = crea_schermata_impostazioni_standard("IMPOSTA ORARIO");

    time_t now_time = time(NULL);
    struct tm now;
    localtime_r(&now_time, &now);

    // ============================================
    // CONTENUTO SOTTO LA LINEA (Y > 150)
    // ============================================

    // SLIDER ORE con bottoni agli estremi
    slider_ore = lv_slider_create(schermo_impostazioni_orologio);
    lv_slider_set_range(slider_ore, 0, 23);
    lv_slider_set_value(slider_ore, now.tm_hour, LV_ANIM_OFF);
    lv_obj_set_width(slider_ore, 250);
    lv_obj_set_height(slider_ore, 25);
    lv_obj_align(slider_ore, LV_ALIGN_CENTER, 0, -40);  // Posizione relativa al centro, sotto la linea
    lv_obj_set_style_bg_color(slider_ore, lv_color_hex(0x0F73B5), LV_PART_INDICATOR);
    lv_obj_add_event_cb(slider_ore, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Bottoni + e - per ore agli estremi
    lv_obj_t *btn_plus_ore, *btn_minus_ore;
    crea_bottoni_plus_minus_estremi(schermo_impostazioni_orologio, slider_ore, &btn_plus_ore, &btn_minus_ore);
    lv_obj_add_event_cb(btn_plus_ore, btn_plus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_minus_ore, btn_minus_cb, LV_EVENT_CLICKED, NULL);

    // Label ore posizionata sotto lo slider
    label_ore = lv_label_create(schermo_impostazioni_orologio);
    lv_obj_align_to(label_ore, slider_ore, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // SLIDER MINUTI con bottoni agli estremi
    slider_minuti = lv_slider_create(schermo_impostazioni_orologio);
    lv_slider_set_range(slider_minuti, 0, 59);
    lv_slider_set_value(slider_minuti, now.tm_min, LV_ANIM_OFF);
    lv_obj_set_width(slider_minuti, 250);
    lv_obj_set_height(slider_minuti, 25);
    lv_obj_align(slider_minuti, LV_ALIGN_CENTER, 0, 20);  // Posizionato sotto slider ore
    lv_obj_set_style_bg_color(slider_minuti, lv_color_hex(0x0F73B5), LV_PART_INDICATOR);
    lv_obj_add_event_cb(slider_minuti, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Bottoni + e - per minuti agli estremi
    lv_obj_t *btn_plus_minuti, *btn_minus_minuti;
    crea_bottoni_plus_minus_estremi(schermo_impostazioni_orologio, slider_minuti, &btn_plus_minuti, &btn_minus_minuti);
    lv_obj_add_event_cb(btn_plus_minuti, btn_plus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_minus_minuti, btn_minus_cb, LV_EVENT_CLICKED, NULL);

    // Label minuti posizionata sotto lo slider
    label_minuti = lv_label_create(schermo_impostazioni_orologio);
    lv_obj_align_to(label_minuti, slider_minuti, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    aggiorna_label_orario();

    // Bottone Salva in basso
    lv_obj_t *btn_salva = lv_btn_create(schermo_impostazioni_orologio);
    lv_obj_set_size(btn_salva, 120, 50);
    lv_obj_align(btn_salva, LV_ALIGN_BOTTOM_MID, 0, -30);  // Centrato in basso
    lv_obj_t *label_btn = lv_label_create(btn_salva);
    lv_label_set_text(label_btn, "Salva");
    lv_obj_center(label_btn);
    lv_obj_set_style_bg_color(btn_salva, lv_color_hex(0x0F73B5), 0);  // Colore tema
    lv_obj_add_event_cb(btn_salva, salva_orario_cb, LV_EVENT_CLICKED, NULL);

    lv_scr_load(schermo_impostazioni_orologio);
  });
}

lv_obj_t *crea_schermata_generica(const char *titolo) {
  // Questa funzione ora usa il nuovo layout standard
  return crea_schermata_impostazioni_standard(titolo);
}

void cancella_arco() {
  if (lockErogazione) {
    safe_lvgl_operation([&]() {
      if (arco_timer) {
        lv_anim_del(arco_timer, (lv_anim_exec_xcb_t)lv_arc_set_value);
        lv_obj_del(arco_timer);
        arco_timer = NULL;
      }
    });

    ferma_erogazione_valvole();

    lockErogazione = 0;
    lv_obj_add_event_cb(schermo1, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(schermo2, gesture_cb, LV_EVENT_GESTURE, NULL);
  }
}

void aggiorna_orologio() {
  safe_lvgl_operation([&]() {
    static char buffer[16];
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    lv_label_set_text(label_orologio, buffer);
  });
}

void setup_rtc() {
  struct tm t;
  t.tm_year = 2025 - 1900;
  t.tm_mon = 6 - 1;
  t.tm_mday = 1;
  t.tm_hour = 12;
  t.tm_min = 0;
  t.tm_sec = 0;
  time_t time_since_epoch = mktime(&t);
  struct timeval now = { .tv_sec = time_since_epoch, .tv_usec = 0 };
  settimeofday(&now, NULL);
}

static void salva_orario_cb(lv_event_t *e) {
  int ore = lv_slider_get_value(slider_ore);
  int minuti = lv_slider_get_value(slider_minuti);
  int secondi = 0;  // SEMPRE 0 - RIMOSSO LO SLIDER

  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  timeinfo.tm_hour = ore;
  timeinfo.tm_min = minuti;
  timeinfo.tm_sec = secondi;

  time_t nuovo_orario = mktime(&timeinfo);
  struct timeval tv = { .tv_sec = nuovo_orario, .tv_usec = 0 };
  settimeofday(&tv, NULL);

  safe_lvgl_operation([&]() {
    lv_scr_load_anim(schermo2, LV_SCR_LOAD_ANIM_FADE_IN, 150, 0, false);
  });
}

void aggiorna_label_orario() {
  safe_lvgl_operation([&]() {
    static char buf[32];
    snprintf(buf, sizeof(buf), "Ore: %d", lv_slider_get_value(slider_ore));
    lv_label_set_text(label_ore, buf);
    snprintf(buf, sizeof(buf), "Minuti: %d", lv_slider_get_value(slider_minuti));
    lv_label_set_text(label_minuti, buf);
    // RIMOSSA LA LABEL SECONDI
  });
}

// ============================================
// SCHERMATA CONTATORE CON TABELLA FORMATTATA
// ============================================

void callback_bottone_impostazioni_contatore(lv_event_t *evento) {
  safe_lvgl_operation([&]() {
    safe_screen_delete(schermo_contatore);

    // Usa la nuova funzione helper per layout standard
    schermo_contatore = crea_schermata_impostazioni_standard("CONTATORE");

    // ============================================
    // CONTENUTO SOTTO LA LINEA (Y > 150) - LAYOUT COMPATTO
    // ============================================

    // Container principale per la tabella (dimensioni estese verso il basso)
    lv_obj_t *container_tabella = lv_obj_create(schermo_contatore);
    lv_obj_set_size(container_tabella, 720, 220);             // ESTESO: Altezza aumentata da 180 a 220
    lv_obj_align(container_tabella, LV_ALIGN_CENTER, 0, 20);  // Posizionato sotto la linea Y=150
    lv_obj_set_style_bg_color(container_tabella, lv_color_white(), 0);
    lv_obj_set_style_border_color(container_tabella, lv_color_hex(0x0F73B5), 0);
    lv_obj_set_style_border_width(container_tabella, 2, 0);
    lv_obj_set_style_radius(container_tabella, 8, 0);
    lv_obj_set_style_pad_all(container_tabella, 5, 0);  // Padding minimo

    // ============================================
    // HEADER TABELLA
    // ============================================

    // Container header
    lv_obj_t *header_container = lv_obj_create(container_tabella);
    lv_obj_set_size(header_container, 710, 40);  // ESTESO: Altezza leggermente aumentata
    lv_obj_align(header_container, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header_container, lv_color_hex(0x0F73B5), 0);
    lv_obj_set_style_radius(header_container, 5, 0);
    lv_obj_set_style_border_width(header_container, 0, 0);
    lv_obj_set_style_pad_all(header_container, 0, 0);

    // Headers
    lv_obj_t *header1 = lv_label_create(header_container);
    lv_label_set_text(header1, "Contenitore");
    lv_obj_align(header1, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_text_color(header1, lv_color_white(), 0);
    lv_obj_set_style_text_font(header1, &lv_font_montserrat_14, 0);

    lv_obj_t *header2 = lv_label_create(header_container);
    lv_label_set_text(header2, "Litri erogati");
    lv_obj_align(header2, LV_ALIGN_LEFT_MID, 250, 0);
    lv_obj_set_style_text_color(header2, lv_color_white(), 0);
    lv_obj_set_style_text_font(header2, &lv_font_montserrat_14, 0);

    lv_obj_t *header3 = lv_label_create(header_container);
    lv_label_set_text(header3, "Bottiglie risparmiate");
    lv_obj_align(header3, LV_ALIGN_LEFT_MID, 450, 0);
    lv_obj_set_style_text_color(header3, lv_color_white(), 0);
    lv_obj_set_style_text_font(header3, &lv_font_montserrat_14, 0);

    // Linee verticali separatrici header
    lv_obj_t *linea_vert_header1 = lv_obj_create(header_container);
    lv_obj_set_size(linea_vert_header1, 2, 30);  // ESTESO: Altezza aumentata
    lv_obj_align(linea_vert_header1, LV_ALIGN_LEFT_MID, 220, 0);
    lv_obj_set_style_bg_color(linea_vert_header1, lv_color_white(), 0);
    lv_obj_set_style_border_width(linea_vert_header1, 0, 0);
    lv_obj_clear_flag(linea_vert_header1, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *linea_vert_header2 = lv_obj_create(header_container);
    lv_obj_set_size(linea_vert_header2, 2, 30);  // ESTESO: Altezza aumentata
    lv_obj_align(linea_vert_header2, LV_ALIGN_LEFT_MID, 420, 0);
    lv_obj_set_style_bg_color(linea_vert_header2, lv_color_white(), 0);
    lv_obj_set_style_border_width(linea_vert_header2, 0, 0);
    lv_obj_clear_flag(linea_vert_header2, LV_OBJ_FLAG_CLICKABLE);

    // ============================================
    // RIGHE TABELLA
    // ============================================

    // Calcoli per le righe
    float litri_bicchiere = (litri_erogati_totali * 0.25f);   // 25% bicchieri
    float litri_bottiglina = (litri_erogati_totali * 0.35f);  // 35% bottigline
    float litri_bottiglia = (litri_erogati_totali * 0.40f);   // 40% bottiglie

    float bottiglie_bicchiere = litri_bicchiere * 4.0f;    // 1L = 4 bicchieri da 250ml
    float bottiglie_bottiglina = litri_bottiglina * 2.0f;  // 1L = 2 bottigline da 500ml
    float bottiglie_bottiglia = litri_bottiglia * 1.0f;    // 1L = 1 bottiglia da 1000ml

    // Dati per le righe
    struct {
      const char *nome;
      float litri;
      float bottiglie;
      uint32_t colore_bg;
    } righe[] = {
      { "Bicchiere 250ml", litri_bicchiere, bottiglie_bicchiere, 0xF8F9FA },
      { "Bottiglina 500ml", litri_bottiglina, bottiglie_bottiglina, 0xFFFFFF },
      { "Bottiglia 1000ml", litri_bottiglia, bottiglie_bottiglia, 0xF8F9FA }
    };

    // Crea le righe con dimensioni leggermente estese
    for (int i = 0; i < 3; i++) {
      // Container riga
      lv_obj_t *riga = lv_obj_create(container_tabella);
      lv_obj_set_size(riga, 710, 40);                          // ESTESO: Altezza aumentata da 35 a 40px
      lv_obj_align(riga, LV_ALIGN_TOP_MID, 0, 45 + (i * 40));  // ESTESO: Spaziatura aumentata
      lv_obj_set_style_bg_color(riga, lv_color_hex(righe[i].colore_bg), 0);
      lv_obj_set_style_border_width(riga, 0, 0);
      lv_obj_set_style_radius(riga, 0, 0);
      lv_obj_set_style_pad_all(riga, 0, 0);

      // Linea separatrice tra righe (tranne ultima)
      if (i < 2) {
        lv_obj_t *linea = lv_obj_create(riga);
        lv_obj_set_size(linea, 700, 1);
        lv_obj_align(linea, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(linea, lv_color_hex(0xDEE2E6), 0);
        lv_obj_set_style_border_width(linea, 0, 0);
      }

      // Linee verticali separatrici righe
      lv_obj_t *linea_vert1 = lv_obj_create(riga);
      lv_obj_set_size(linea_vert1, 2, 30);  // ESTESO: Altezza aumentata
      lv_obj_align(linea_vert1, LV_ALIGN_LEFT_MID, 220, 0);
      lv_obj_set_style_bg_color(linea_vert1, lv_color_hex(0xDEE2E6), 0);
      lv_obj_set_style_border_width(linea_vert1, 0, 0);
      lv_obj_clear_flag(linea_vert1, LV_OBJ_FLAG_CLICKABLE);

      lv_obj_t *linea_vert2 = lv_obj_create(riga);
      lv_obj_set_size(linea_vert2, 2, 30);  // ESTESO: Altezza aumentata
      lv_obj_align(linea_vert2, LV_ALIGN_LEFT_MID, 420, 0);
      lv_obj_set_style_bg_color(linea_vert2, lv_color_hex(0xDEE2E6), 0);
      lv_obj_set_style_border_width(linea_vert2, 0, 0);
      lv_obj_clear_flag(linea_vert2, LV_OBJ_FLAG_CLICKABLE);

      // Colonne
      // Colonna 1: Nome contenitore
      lv_obj_t *col1 = lv_label_create(riga);
      lv_label_set_text(col1, righe[i].nome);
      lv_obj_align(col1, LV_ALIGN_LEFT_MID, 20, 0);
      lv_obj_set_style_text_font(col1, &lv_font_montserrat_14, 0);
      lv_obj_set_style_text_color(col1, lv_color_hex(0x333333), 0);

      // Colonna 2: Litri erogati
      lv_obj_t *col2 = lv_label_create(riga);
      static char buffer_litri[20];
      snprintf(buffer_litri, sizeof(buffer_litri), "%.2f L", righe[i].litri);
      lv_label_set_text(col2, buffer_litri);
      lv_obj_align(col2, LV_ALIGN_LEFT_MID, 280, 0);
      lv_obj_set_style_text_font(col2, &lv_font_montserrat_14, 0);
      lv_obj_set_style_text_color(col2, lv_color_hex(0x333333), 0);

      // Colonna 3: Bottiglie risparmiate
      lv_obj_t *col3 = lv_label_create(riga);
      static char buffer_bottiglie[30];
      snprintf(buffer_bottiglie, sizeof(buffer_bottiglie), "%.0f bottiglie", righe[i].bottiglie);
      lv_label_set_text(col3, buffer_bottiglie);
      lv_obj_align(col3, LV_ALIGN_LEFT_MID, 480, 0);
      lv_obj_set_style_text_font(col3, &lv_font_montserrat_14, 0);
      lv_obj_set_style_text_color(col3, lv_color_hex(0x333333), 0);
    }

    // ============================================
    // RIGA TOTALI (evidenziata)
    // ============================================

    lv_obj_t *riga_totale = lv_obj_create(container_tabella);
    lv_obj_set_size(riga_totale, 710, 45);                // ESTESO: Altezza aumentata da 40 a 45px
    lv_obj_align(riga_totale, LV_ALIGN_TOP_MID, 0, 165);  // ESTESO: Posizione aggiustata (45 + 3*40 = 165)
    lv_obj_set_style_bg_color(riga_totale, lv_color_hex(0x0F73B5), 0);
    lv_obj_set_style_radius(riga_totale, 5, 0);
    lv_obj_set_style_border_width(riga_totale, 0, 0);
    lv_obj_set_style_pad_all(riga_totale, 0, 0);

    // Linee verticali separatrici riga totale
    lv_obj_t *linea_vert_tot1 = lv_obj_create(riga_totale);
    lv_obj_set_size(linea_vert_tot1, 2, 35);  // ESTESO: Altezza aumentata
    lv_obj_align(linea_vert_tot1, LV_ALIGN_LEFT_MID, 220, 0);
    lv_obj_set_style_bg_color(linea_vert_tot1, lv_color_white(), 0);
    lv_obj_set_style_border_width(linea_vert_tot1, 0, 0);
    lv_obj_clear_flag(linea_vert_tot1, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *linea_vert_tot2 = lv_obj_create(riga_totale);
    lv_obj_set_size(linea_vert_tot2, 2, 35);  // ESTESO: Altezza aumentata
    lv_obj_align(linea_vert_tot2, LV_ALIGN_LEFT_MID, 420, 0);
    lv_obj_set_style_bg_color(linea_vert_tot2, lv_color_white(), 0);
    lv_obj_set_style_border_width(linea_vert_tot2, 0, 0);
    lv_obj_clear_flag(linea_vert_tot2, LV_OBJ_FLAG_CLICKABLE);

    // Totali
    lv_obj_t *label_totale_nome = lv_label_create(riga_totale);
    lv_label_set_text(label_totale_nome, "TOTALE");
    lv_obj_align(label_totale_nome, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_text_font(label_totale_nome, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label_totale_nome, lv_color_white(), 0);

    lv_obj_t *label_totale_litri = lv_label_create(riga_totale);
    static char buffer_totale_litri[20];
    snprintf(buffer_totale_litri, sizeof(buffer_totale_litri), "%.2f L", litri_erogati_totali);
    lv_label_set_text(label_totale_litri, buffer_totale_litri);
    lv_obj_align(label_totale_litri, LV_ALIGN_LEFT_MID, 280, 0);
    lv_obj_set_style_text_font(label_totale_litri, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label_totale_litri, lv_color_white(), 0);

    lv_obj_t *label_totale_bottiglie = lv_label_create(riga_totale);
    static char buffer_totale_bottiglie[30];
    snprintf(buffer_totale_bottiglie, sizeof(buffer_totale_bottiglie), "%.0f bottiglie", calcola_bottiglie_risparmiate());
    lv_label_set_text(label_totale_bottiglie, buffer_totale_bottiglie);
    lv_obj_align(label_totale_bottiglie, LV_ALIGN_LEFT_MID, 480, 0);
    lv_obj_set_style_text_font(label_totale_bottiglie, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label_totale_bottiglie, lv_color_white(), 0);

    // ============================================
    // RIGA INFERIORE: BOTTONI + INFO DENARO (STESSA LINEA)
    // ============================================

    // Bottone Reset (sinistra)
    lv_obj_t *btn_reset = lv_btn_create(schermo_contatore);
    lv_obj_set_size(btn_reset, 120, 50);
    lv_obj_align(btn_reset, LV_ALIGN_BOTTOM_LEFT, 40, -20);
    lv_obj_t *label_reset = lv_label_create(btn_reset);
    lv_label_set_text(label_reset, "RESET");
    lv_obj_center(label_reset);
    lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_text_font(label_reset, &lv_font_montserrat_14, 0);

    lv_obj_add_event_cb(
      btn_reset, [](lv_event_t *e) {
        safe_lvgl_operation([&]() {
          static const char *btns[] = { "Annulla", "Conferma", "" };
          lv_obj_t *mbox = lv_msgbox_create(NULL, "Conferma Reset",
                                            "Vuoi resettare tutti i dati del contatore?",
                                            btns, true);
          lv_obj_center(mbox);

          lv_obj_add_event_cb(
            mbox, [](lv_event_t *e) {
              lv_obj_t *obj = lv_event_get_current_target(e);
              uint32_t id = lv_msgbox_get_active_btn(obj);
              if (id == 1) {
                reset_contatore();
                // Ricarica la schermata per aggiornare i dati
                callback_bottone_impostazioni_contatore(NULL);
              }
              lv_msgbox_close(obj);
            },
            LV_EVENT_VALUE_CHANGED, NULL);
        });
      },
      LV_EVENT_CLICKED, NULL);

    // Info denaro risparmiato (centro, stessa linea dei bottoni)
    lv_obj_t *label_denaro = lv_label_create(schermo_contatore);
    static char buffer_denaro[50];
    snprintf(buffer_denaro, sizeof(buffer_denaro), "💰 Risparmiati: %.2f €", calcola_denaro_risparmiato());
    lv_label_set_text(label_denaro, buffer_denaro);
    lv_obj_align(label_denaro, LV_ALIGN_BOTTOM_MID, 0, -35);  // STESSA LINEA: Centrato tra i bottoni
    lv_obj_set_style_text_font(label_denaro, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label_denaro, lv_color_hex(0x28A745), 0);  // Verde

    // Bottone Dettagli (destra)
    lv_obj_t *btn_dettagli = lv_btn_create(schermo_contatore);
    lv_obj_set_size(btn_dettagli, 120, 50);
    lv_obj_align(btn_dettagli, LV_ALIGN_BOTTOM_RIGHT, -40, -20);
    lv_obj_t *label_dettagli = lv_label_create(btn_dettagli);
    lv_label_set_text(label_dettagli, "Dettagli");
    lv_obj_center(label_dettagli);
    lv_obj_set_style_bg_color(btn_dettagli, lv_color_hex(0x0F73B5), 0);
    lv_obj_set_style_text_font(label_dettagli, &lv_font_montserrat_14, 0);

    lv_obj_add_event_cb(
      btn_dettagli, [](lv_event_t *e) {
        safe_lvgl_operation([&]() {
          static char dettagli[300];
          snprintf(dettagli, sizeof(dettagli),
                   "DETTAGLI CONTATORE:\n\n"
                   "📊 CAPACITÀ CONTENITORI:\n"
                   "• Bicchiere: 250ml\n"
                   "• Bottiglina: 500ml\n"
                   "• Bottiglia: 1000ml\n\n"
                   "💰 CALCOLO RISPARMIO:\n"
                   "• 1 litro = 2 bottiglie da 500ml\n"
                   "• Prezzo bottiglia: 0.30€\n\n"
                   "🏆 TOTALE EROGATO:\n"
                   "• %.2f litri\n"
                   "• %.0f bottiglie risparmiate\n"
                   "• %.2f € risparmiati",
                   litri_erogati_totali,
                   calcola_bottiglie_risparmiate(),
                   calcola_denaro_risparmiato());

          lv_obj_t *mbox = lv_msgbox_create(NULL, "Info Contatore", dettagli, NULL, true);
          lv_obj_center(mbox);
        });
      },
      LV_EVENT_CLICKED, NULL);

    lv_scr_load(schermo_contatore);
  });
}

static void slider_event_erogazione_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  int val = lv_slider_get_value(slider);

  safe_lvgl_operation([&]() {
    if (slider == slider_bicchiere_erogazione) {
      tempoContenitore[0] = val;
      static char buf[32];
      snprintf(buf, sizeof(buf), "Bicchiere: %d s", val);
      lv_label_set_text(label_bicchiere_erogazione, buf);
    } else if (slider == slider_bottiglina_erogazione) {
      tempoContenitore[1] = val;
      static char buf[32];
      snprintf(buf, sizeof(buf), "Bottiglina: %d s", val);
      lv_label_set_text(label_bottiglina_erogazione, buf);
    } else if (slider == slider_bottiglia_erogazione) {
      tempoContenitore[2] = val;
      static char buf[32];
      snprintf(buf, sizeof(buf), "Bottiglia: %d s", val);
      lv_label_set_text(label_bottiglia_erogazione, buf);
    }
  });
}

static void slider_event_cb(lv_event_t *e) {
  aggiorna_label_orario();
}

void mostra_errore_sd(const char *msg) {
  safe_lvgl_operation([&]() {
    lv_obj_t *mbox = lv_msgbox_create(NULL, "Errore", msg, NULL, true);
    lv_obj_center(mbox);
  });
}

// ============================================
// LOOP PRINCIPALE OTTIMIZZATO E THREAD-SAFE
// ============================================

void loop() {
  // Reset WDT solo se è stato inizializzato correttamente
  if (wdt_initialized) {
    esp_task_wdt_reset();
  }

  // Heartbeat del sistema
  monitor.heartbeat();

  // Timing ottimizzato per le operazioni
  static unsigned long last_operations[4] = { 0, 0, 0, 0 };
  unsigned long now = millis();

  // Orologio ogni secondo
  if (now - last_operations[0] > 1000) {
    aggiorna_orologio();
    last_operations[0] = now;
  }

  // Controllo filtro ogni 30 secondi (non più ogni ora!)
  static bool filtro_inizializzato = false;
  if (!filtro_inizializzato && now > 10000) {
    filtro_inizializzato = true;
    Serial.println("Inizializzazione ritardata sistema filtri...");
    carica_dati_filtro();
    carica_impostazioni_allarmi();
    Serial.println("✅ Sistema filtri inizializzato");
  }

  if (filtro_inizializzato && (now - last_operations[1] > 30000)) {
    controlla_stato_filtro();
    last_operations[1] = now;
  }

  // Health check ogni 30 secondi
  if (now - last_operations[2] > SYSTEM_HEALTH_CHECK_INTERVAL) {
    monitor.check_health();
    last_operations[2] = now;
  }

  // Statistiche memoria ogni 5 minuti
  if (now - last_operations[3] > 300000) {
    monitor.print_stats();
    last_operations[3] = now;
  }

  // Yield per permettere ad altre task di eseguire
  vTaskDelay(pdMS_TO_TICKS(50));
}