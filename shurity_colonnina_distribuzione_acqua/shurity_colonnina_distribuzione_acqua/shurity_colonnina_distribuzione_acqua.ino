#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"

#define MAX_LARGHEZZA 800
#define MAX_ALTEZZA 480

#define SIZE_ICON_SMALL 64
#define SIZE_ICON_CERCHIATA 99
#define SIZE_ICON_BIG 128

extern const lv_img_dsc_t acqua_fresca, acqua_frizzante, acqua_liscia, bicchiere, bottiglina, bottiglia, acqua_fresca_selezionata, acqua_frizzante_selezionata, acqua_liscia_selezionata, bicchiere_selezionata, bottiglina_selezionata, bottiglia_selezionata, freccia_cerchiata, impostazioni_allarme, impostazioni_aspetto, impostazioni_bicchiere, impostazioni_contatore, impostazioni_schermo, ingranaggio, ingranaggio_cerchiato, spunta, spunta_cerchiata;

using namespace esp_panel::drivers;
using namespace esp_panel::board;

// oggetti globali super-scope
//lv_obj_t *debugLabel;
lv_obj_t *sliderBottiglia, *etichettaSliderBottiglia;
lv_obj_t *sliderBicchiere, *etichettaSliderBicchiere;
lv_obj_t *bottone_bicchiere;

// variabili globali
uint8_t tempoBottiglia = 10;  // in secondi, massimo 128
uint8_t tempoBicchiere = 5;   // come sopra
bool bottigliaBicchiere = 0;
uint8_t tempoContenitore[] = { 10, 20, 30 };  // 0 bicchiere - 1 bottiglina - 2 bottiglia
uint8_t tipoAcqua = 0;                        // 0 liscia - 1 fresca - 2 frizzante
bool lockErogazione = 0;

// definizione oggetti LVGL che verranno usati in seguito
lv_obj_t *schermo1;
lv_obj_t *schermo2;

lv_obj_t *debug_label;
lv_obj_t *bottone_acqua_liscia;
lv_obj_t *bottone_acqua_fresca;
lv_obj_t *bottone_acqua_frizzante;
lv_obj_t *bottone_bottiglina;
lv_obj_t *bottone_bottiglia;
lv_obj_t *cerchio_selezione;
lv_obj_t *arco_timer;
lv_obj_t *bottone_impostazioni_schermo;
lv_obj_t *bottone_impostazioni_bicchiere;
lv_obj_t *bottone_impostazioni_aspetto;
lv_obj_t *bottone_impostazioni_contatore;
lv_obj_t *bottone_impostazioni_allarme;

// prototipi delle funzioni, callback e di utilità varia..
void callback_bottone_acqua_liscia(lv_event_t *evento);
void callback_bottone_acqua_fresca(lv_event_t *evento);
void callback_bottone_acqua_frizzante(lv_event_t *evento);
void callback_bottone_bicchiere(lv_event_t *evento);
void callback_bottone_bottiglina(lv_event_t *evento);
void callback_bottone_bottiglia(lv_event_t *evento);
void callback_bottone_impostazioni_schermo(lv_event_t *evento);
void callback_bottone_impostazioni_bicchiere(lv_event_t *evento);
void callback_bottone_impostazioni_aspetto(lv_event_t *evento);
void callback_bottone_impostazioni_contatore(lv_event_t *evento);
void callback_bottone_impostazioni_allarme(lv_event_t *evento);
void deseleziona_bottoni();
void evidenzia_bottone(lv_obj_t *bersaglio);
void aggiorna_debug_label(uint8_t *valore);
void erogazione(uint8_t tempo, lv_obj_t *target);
static void gesture_cb(lv_event_t *e);
lv_obj_t *crea_schermata_generica(const char *titolo);

void setup() {
  Serial.begin(115200);

  Serial.println("Initializing board");
  Board *board = new Board();
  board->init();

#if LVGL_PORT_AVOID_TEARING_MODE
  auto lcd = board->getLCD();
  // When avoid tearing function is enabled, the frame buffer number should be set in the board driver
  lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
  auto lcd_bus = lcd->getBus();
  /**
     * As the anti-tearing feature typically consumes more PSRAM bandwidth, for the ESP32-S3, we need to utilize the
     * "bounce buffer" functionality to enhance the RGB data bandwidth.
     * This feature will consume `bounce_buffer_size * bytes_per_pixel * 2` of SRAM memory.
     */
  if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
    static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
  }
#endif
#endif
  assert(board->begin());

  Serial.println("Initializing LVGL");
  lvgl_port_init(board->getLCD(), board->getTouch());

  Serial.println("Creating UI");
  /* Lock the mutex due to the LVGL APIs are not thread-safe */
  lvgl_port_lock(-1);

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  //                                                        creazione oggetti                                                         //
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // creo lo schermo1
  schermo1 = lv_obj_create(schermo1);
  schermo2 = lv_obj_create(NULL);

  lv_obj_add_event_cb(schermo1, gesture_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(schermo2, gesture_cb, LV_EVENT_GESTURE, NULL);

  // etichetta per valori di debug, la spengo e accendo all'occorrenza
  /*   debug_label = lv_label_create(schermo2);
  lv_obj_align(debug_label, LV_ALIGN_BOTTOM_LEFT, 20, -20); */

  // creazione cerchio di selezione per i bottoni del tipo d'acqua
  cerchio_selezione = lv_obj_create(schermo1);
  lv_obj_set_size(cerchio_selezione, SIZE_ICON_BIG + 20, SIZE_ICON_BIG + 20);
  lv_obj_set_style_radius(cerchio_selezione, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(cerchio_selezione, lv_color_hex(0x00AEEF), 0);  // Azzurro
  lv_obj_set_style_bg_opa(cerchio_selezione, LV_OPA_30, 0);                 // Semitrasparente
  lv_obj_clear_flag(cerchio_selezione, LV_OBJ_FLAG_CLICKABLE);              // Non interagisce
  lv_obj_move_background(cerchio_selezione);                                // Sta dietro i bottoni

#define PRIMA_RIGA 160
#define SECONDA_RIGA 320
#define PRIMA_COLONNA 120
#define SECONDA_COLONNA 340
#define TERZA_COLONNA 560

  // creazione dei tre pulsanti (per riga) per l'erogazione del tipo di acqua
  bottone_acqua_liscia = lv_imgbtn_create(schermo1);
  lv_imgbtn_set_src(bottone_acqua_liscia, LV_IMGBTN_STATE_RELEASED, NULL, &acqua_liscia, NULL);
  lv_obj_set_size(bottone_acqua_liscia, SIZE_ICON_BIG, SIZE_ICON_BIG);
  lv_obj_align(bottone_acqua_liscia, NULL, PRIMA_COLONNA, PRIMA_RIGA);

  bottone_acqua_fresca = lv_imgbtn_create(schermo1);
  lv_imgbtn_set_src(bottone_acqua_fresca, LV_IMGBTN_STATE_RELEASED, NULL, &acqua_fresca, NULL);
  lv_obj_set_size(bottone_acqua_fresca, SIZE_ICON_BIG, SIZE_ICON_BIG);
  lv_obj_align(bottone_acqua_fresca, NULL, SECONDA_COLONNA, PRIMA_RIGA);

  bottone_acqua_frizzante = lv_imgbtn_create(schermo1);
  lv_imgbtn_set_src(bottone_acqua_frizzante, LV_IMGBTN_STATE_RELEASED, NULL, &acqua_frizzante, NULL);
  lv_obj_set_size(bottone_acqua_frizzante, SIZE_ICON_BIG, SIZE_ICON_BIG);
  lv_obj_align(bottone_acqua_frizzante, NULL, TERZA_COLONNA, PRIMA_RIGA);

  // i tre bottoni per selezionare il tempo di erogazione:
  bottone_bicchiere = lv_imgbtn_create(schermo1);
  lv_imgbtn_set_src(bottone_bicchiere, LV_IMGBTN_STATE_RELEASED, 0, &bicchiere, 0);
  lv_obj_set_size(bottone_bicchiere, SIZE_ICON_BIG, SIZE_ICON_BIG);
  lv_obj_align(bottone_bicchiere, NULL, PRIMA_COLONNA, SECONDA_RIGA);

  bottone_bottiglina = lv_imgbtn_create(schermo1);
  lv_imgbtn_set_src(bottone_bottiglina, LV_IMGBTN_STATE_RELEASED, 0, &bottiglina, 0);
  lv_obj_set_size(bottone_bottiglina, SIZE_ICON_BIG, SIZE_ICON_BIG);
  lv_obj_align(bottone_bottiglina, NULL, SECONDA_COLONNA, SECONDA_RIGA);

  bottone_bottiglia = lv_imgbtn_create(schermo1);
  lv_imgbtn_set_src(bottone_bottiglia, LV_IMGBTN_STATE_RELEASED, 0, &bottiglia, 0);
  lv_obj_set_size(bottone_bottiglia, SIZE_ICON_BIG, SIZE_ICON_BIG);
  lv_obj_align(bottone_bottiglia, NULL, TERZA_COLONNA, SECONDA_RIGA);

  // creazione dei 5 pulsanti impostazioni sulla schermata 2:
  bottone_impostazioni_schermo = lv_imgbtn_create(schermo2);
  lv_imgbtn_set_src(bottone_impostazioni_schermo, LV_IMGBTN_STATE_RELEASED, 0, &impostazioni_schermo, 0);
  lv_obj_set_size(bottone_impostazioni_schermo, SIZE_ICON_BIG, SIZE_ICON_BIG);
  lv_obj_align(bottone_impostazioni_schermo, NULL, PRIMA_COLONNA, PRIMA_RIGA);

  bottone_impostazioni_bicchiere = lv_imgbtn_create(schermo2);
  lv_imgbtn_set_src(bottone_impostazioni_bicchiere, LV_IMGBTN_STATE_RELEASED, 0, &bicchiere, 0);
  lv_obj_set_size(bottone_impostazioni_bicchiere, SIZE_ICON_BIG, SIZE_ICON_BIG);
  lv_obj_align(bottone_impostazioni_bicchiere, NULL, SECONDA_COLONNA, PRIMA_RIGA);

  bottone_impostazioni_aspetto = lv_imgbtn_create(schermo2);
  lv_imgbtn_set_src(bottone_impostazioni_aspetto, LV_IMGBTN_STATE_RELEASED, 0, &impostazioni_aspetto, 0);
  lv_obj_set_size(bottone_impostazioni_aspetto, SIZE_ICON_BIG, SIZE_ICON_BIG);
  lv_obj_align(bottone_impostazioni_aspetto, NULL, TERZA_COLONNA, PRIMA_RIGA);

  bottone_impostazioni_contatore = lv_imgbtn_create(schermo2);
  lv_imgbtn_set_src(bottone_impostazioni_contatore, LV_IMGBTN_STATE_RELEASED, 0, &impostazioni_contatore, 0);
  lv_obj_set_size(bottone_impostazioni_contatore, SIZE_ICON_BIG, SIZE_ICON_BIG);
  lv_obj_align(bottone_impostazioni_contatore, NULL, PRIMA_COLONNA, SECONDA_RIGA);

  bottone_impostazioni_allarme = lv_imgbtn_create(schermo2);
  lv_imgbtn_set_src(bottone_impostazioni_allarme, LV_IMGBTN_STATE_RELEASED, 0, &impostazioni_allarme, 0);
  lv_obj_set_size(bottone_impostazioni_allarme, SIZE_ICON_BIG, SIZE_ICON_BIG);
  lv_obj_align(bottone_impostazioni_allarme, NULL, SECONDA_COLONNA, SECONDA_RIGA);

  // colleghiamo le callback ai vari elementi:
  // ESEMPIO: lv_obj_add_event_cb(acquaLiscia, callback_pulsante_tipo_acqua, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_acqua_liscia, callback_bottone_acqua_liscia, LV_EVENT_CLICKED, NULL);  // LV_EVENT_VALUE_CHANGED
  lv_obj_add_event_cb(bottone_acqua_fresca, callback_bottone_acqua_fresca, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_acqua_frizzante, callback_bottone_acqua_frizzante, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_bicchiere, callback_bottone_bicchiere, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_bottiglina, callback_bottone_bottiglina, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_bottiglia, callback_bottone_bottiglia, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_impostazioni_schermo, callback_bottone_impostazioni_schermo, LV_EVENT_CLICKED, NULL);
  /*   lv_obj_add_event_cb(bottone_impostazioni_bicchiere, callback_bottone_impostazioni_bicchiere, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_impostazioni_aspetto, callback_bottone_impostazioni_aspetto, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_impostazioni_contatore, callback_bottone_impostazioni_contatore, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(bottone_impostazioni_allarme, callback_bottone_impostazioni_allarme, LV_EVENT_CLICKED, NULL); */

  // inizializza il bottone dell'acqua liscia selezionato, e lo schermo1
  lv_scr_load(schermo1);
  evidenzia_bottone(bottone_acqua_liscia);
  tipoAcqua = 0;

  /* Release the mutex */
  lvgl_port_unlock();
}

void aggiorna_debug_label(uint8_t valore) {  // funzione di debug per aggiornare l'etichetta che contiene il valore di debug
  static char buffer[32];
  snprintf(buffer, sizeof(buffer), "Valore: %u", valore);  // sostituire qui con il valore che si vuole visualizzare
  lv_label_set_text(debug_label, buffer);
}

void evidenzia_bottone(lv_obj_t *bersaglio) {  // funzione che evidenzia il bottone della scelta acqua, posiziona un cerchio sotto il bottone selezionato
  lv_obj_align_to(cerchio_selezione, bersaglio, LV_ALIGN_CENTER, 0, 0);
}

void erogazione(uint8_t tempo, lv_obj_t *target) {
  lockErogazione = 1;

  // inizializzazione dell'oggetto arco che compie il ciclo d'animazione:
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
  lv_obj_set_style_arc_color(arco_timer, lv_color_hex(0x00AEEF), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arco_timer, 8, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(arco_timer, LV_OPA_COVER, LV_PART_MAIN);

  // Funzione da chiamare quando termina l'animazione:
  auto animazione_completata_cb = [](lv_anim_t *a) {
    if (arco_timer) {
      lv_obj_del(arco_timer);
      arco_timer = NULL;
    }
    lockErogazione = 0;
    // sblocco swipe dello schermo:
    lv_obj_add_event_cb(schermo1, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(schermo2, gesture_cb, LV_EVENT_GESTURE, NULL);
  };

  // prima blocchiamo lo swipe dello schermo:
  lv_obj_remove_event_cb(schermo1, gesture_cb);
  lv_obj_remove_event_cb(schermo2, gesture_cb);

  // animazione:
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, arco_timer);
  lv_anim_set_values(&a, 0, 360);
  lv_anim_set_time(&a, tempo * 1000);  // millisecondi
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_arc_set_value);
  lv_anim_set_path_cb(&a, lv_anim_path_linear);
  lv_anim_set_ready_cb(&a, animazione_completata_cb);  // <== callback alla fine
  lv_anim_start(&a);
}

static void gesture_cb(lv_event_t *e) {
  lv_obj_t *target = lv_event_get_target(e);
  lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

  if (dir == LV_DIR_RIGHT) {
    lv_scr_load_anim(schermo2, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 150, 0, false);
  } else if (dir == LV_DIR_LEFT) {
    lv_scr_load_anim(schermo1, LV_SCR_LOAD_ANIM_MOVE_LEFT, 150, 0, false);
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
    erogazione(tempoContenitore[0], bottone_bicchiere);
  }
}

void callback_bottone_bottiglina(lv_event_t *evento) {
  if (!lockErogazione) {
    erogazione(tempoContenitore[1], bottone_bottiglina);
  }
}

void callback_bottone_bottiglia(lv_event_t *evento) {
  if (!lockErogazione) {
    erogazione(tempoContenitore[2], bottone_bottiglia);
  }
}

void callback_bottone_impostazioni_schermo(lv_event_t *evento) {
  lv_obj_t *schermo_impostazioni = crea_schermata_generica("IMPOSTAZIONI SCHERMO");

  lv_obj_t *sliderLuminosita = lv_slider_create(schermo_impostazioni);
  lv_obj_set_width(sliderLuminosita, 200);
  lv_obj_align(sliderLuminosita, LV_ALIGN_CENTER, 0, 20);

  lv_scr_load_anim(schermo_impostazioni, LV_SCR_LOAD_ANIM_FADE_OUT, 150, 0, false);
}

lv_obj_t *crea_schermata_generica(const char *titolo) {
  lv_obj_t *schermo_generico = lv_obj_create(NULL);

  // creazione freccia indietro:
  lv_obj_t *freccia_indietro = lv_imgbtn_create(schermo_generico);
  lv_imgbtn_set_src(freccia_indietro, LV_IMGBTN_STATE_RELEASED, NULL, &freccia_cerchiata, NULL);
  lv_obj_set_size(freccia_indietro, SIZE_ICON_CERCHIATA, SIZE_ICON_CERCHIATA);
  lv_obj_align(freccia_indietro, LV_ALIGN_TOP_LEFT, 100, 100);
  // lambda della callback della freccia indietro:
  lv_obj_add_event_cb(
    freccia_indietro, [](lv_event_t *evento) {
      lv_scr_load_anim(schermo2, LV_SCR_LOAD_ANIM_FADE_IN, 150, 0, false);
    },
    LV_EVENT_CLICKED, NULL);
  // titolo della schermata:
  lv_obj_t *label = lv_label_create(schermo_generico);
  lv_label_set_text(label, titolo);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);

  return schermo_generico;
}

void loop() {
  delay(10);
}