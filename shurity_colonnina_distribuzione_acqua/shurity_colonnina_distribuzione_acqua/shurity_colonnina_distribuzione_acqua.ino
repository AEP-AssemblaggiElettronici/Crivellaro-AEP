#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"

#define PIN_BIT_1 OD0
#define PIN_BIT_2 OD1

using namespace esp_panel::drivers;
using namespace esp_panel::board;

// prototipi funzione
void callback_pulsante_tipo_acqua(lv_event_t *evento);
void callback_bottiglia(lv_event_t *evento);
void callback_bicchiere(lv_event_t *evento);
void callback_sliderBottiglia(lv_event_t *evento);
void callback_sliderBicchiere(lv_event_t *evento);
lv_obj_t *creazione_bottone_tipo_acqua(const char *nomeBottone, int posX, int posY);

// oggetti globali super-scope
//lv_obj_t *debugLabel;
lv_obj_t *sliderBottiglia, *etichettaSliderBottiglia;
lv_obj_t *sliderBicchiere, *etichettaSliderBicchiere;
HardwareSerial seriale(1);

// variabili globali
uint8_t tempoBottiglia = 10;  // in secondi, massimo 128
uint8_t tempoBicchiere = 5;   // come sopra
bool bottigliaBicchiere = 0;

void setup() {
  String title = "LVGL porting example";

  Serial.begin(115200);
  seriale.begin(9600, SERIAL_8N1, 43, 44);

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
  //debugLabel = lv_label_create(lv_scr_act());
  //lv_obj_align(debugLabel, LV_ALIGN_CENTER, 0, -50);

  // bottone e slider bottiglia:
  // bottone (con etichetta)
  lv_obj_t *bottoneBottiglia = lv_btn_create(lv_scr_act());
  lv_obj_align(bottoneBottiglia, LV_ALIGN_BOTTOM_LEFT, 60, -20);
  lv_obj_t *etichettaBottoneBottiglia = lv_label_create(bottoneBottiglia);
  lv_label_set_text(etichettaBottoneBottiglia, "Tempo erogazione bottiglia");
  lv_obj_center(etichettaBottoneBottiglia);
  lv_obj_add_event_cb(bottoneBottiglia, callback_bottiglia, LV_EVENT_CLICKED, NULL);
  // slider (con etichetta)
  sliderBottiglia = lv_slider_create(lv_scr_act());
  lv_obj_align(sliderBottiglia, LV_ALIGN_CENTER, 0, -30);
  lv_slider_set_range(sliderBottiglia, 1, 120);
  lv_slider_set_value(sliderBottiglia, tempoBottiglia, LV_ANIM_OFF);
  lv_obj_add_event_cb(sliderBottiglia, callback_sliderBottiglia, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_flag(sliderBottiglia, LV_OBJ_FLAG_HIDDEN);
  etichettaSliderBottiglia = lv_label_create(lv_scr_act());
  lv_obj_align(etichettaSliderBottiglia, LV_ALIGN_CENTER, 0, 0);
  static char buf1[16];
  snprintf(buf1, sizeof(buf1), "%lu s", tempoBottiglia);
  lv_label_set_text(etichettaSliderBottiglia, buf1);
  lv_obj_add_flag(etichettaSliderBottiglia, LV_OBJ_FLAG_HIDDEN);

  // bottone e slider bicchiere:
  // bottone (con etichetta)
  lv_obj_t *bottoneBicchiere = lv_btn_create(lv_scr_act());
  lv_obj_align(bottoneBicchiere, LV_ALIGN_BOTTOM_RIGHT, -10, -20);
  lv_obj_t *etichettaBottoneBicchiere = lv_label_create(bottoneBicchiere);
  lv_label_set_text(etichettaBottoneBicchiere, "Tempo erogazione bicchiere");
  lv_obj_center(etichettaBottoneBicchiere);
  lv_obj_add_event_cb(bottoneBicchiere, callback_bicchiere, LV_EVENT_CLICKED, NULL);
  // slider (con etichetta)
  sliderBicchiere = lv_slider_create(lv_scr_act());
  lv_obj_align(sliderBicchiere, LV_ALIGN_CENTER, 0, -30);
  lv_slider_set_range(sliderBicchiere, 1, 120);
  lv_slider_set_value(sliderBicchiere, tempoBicchiere, LV_ANIM_OFF);
  lv_obj_add_event_cb(sliderBicchiere, callback_sliderBicchiere, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_flag(sliderBicchiere, LV_OBJ_FLAG_HIDDEN);
  etichettaSliderBicchiere = lv_label_create(lv_scr_act());
  lv_obj_align(etichettaSliderBicchiere, LV_ALIGN_CENTER, 0, 0);
  static char buf2[16];
  snprintf(buf2, sizeof(buf2), "%lu s", tempoBicchiere);
  lv_label_set_text(etichettaSliderBicchiere, buf2);
  lv_obj_add_flag(etichettaSliderBicchiere, LV_OBJ_FLAG_HIDDEN);

  // creazione dei tre pulsanti per l'erogazione del tipo di acqua
  lv_obj_t *acquaLiscia = creazione_bottone_tipo_acqua("Liscia", 60, 50);
  lv_obj_t *acquaFrizzante = creazione_bottone_tipo_acqua("Frizzante", 300, 50);
  lv_obj_t *tempAmbiente = creazione_bottone_tipo_acqua("Liscia calda", 600, 50);

  // colleghiamo la callback ai 3 bottoni in questione
  lv_obj_add_event_cb(acquaLiscia, callback_pulsante_tipo_acqua, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(acquaFrizzante, callback_pulsante_tipo_acqua, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(tempAmbiente, callback_pulsante_tipo_acqua, LV_EVENT_CLICKED, NULL);

  /* Release the mutex */
  lvgl_port_unlock();
}

void loop() {
  // seriale.write('U'); // TEST
  delay(10);
}

lv_obj_t *creazione_bottone_tipo_acqua(const char *nomeBottone, int posX, int posY) {
  static lv_style_t stileBottoni;
  lv_style_init(&stileBottoni);
  lv_style_set_radius(&stileBottoni, 3);
  lv_style_set_bg_opa(&stileBottoni, LV_OPA_100);
  lv_style_set_bg_color(&stileBottoni, lv_palette_main(LV_PALETTE_GREEN));
  lv_style_set_bg_grad_color(&stileBottoni, lv_palette_darken(LV_PALETTE_GREEN, 2));
  lv_style_set_bg_grad_dir(&stileBottoni, LV_GRAD_DIR_VER);

  lv_style_set_border_opa(&stileBottoni, LV_OPA_40);
  lv_style_set_border_width(&stileBottoni, 2);
  lv_style_set_border_color(&stileBottoni, lv_palette_main(LV_PALETTE_GREY));

  lv_style_set_shadow_width(&stileBottoni, 8);
  lv_style_set_shadow_color(&stileBottoni, lv_palette_main(LV_PALETTE_GREY));
  lv_style_set_shadow_ofs_y(&stileBottoni, 8);

  lv_style_set_outline_opa(&stileBottoni, LV_OPA_COVER);
  lv_style_set_outline_color(&stileBottoni, lv_palette_main(LV_PALETTE_GREEN));

  lv_style_set_text_color(&stileBottoni, lv_color_white());
  lv_style_set_pad_all(&stileBottoni, 10);

  /*Init the pressed style*/
  static lv_style_t stileBottoniPremuti;
  lv_style_init(&stileBottoniPremuti);

  /*Add a large outline when pressed*/
  lv_style_set_outline_width(&stileBottoniPremuti, 30);
  lv_style_set_outline_opa(&stileBottoniPremuti, LV_OPA_TRANSP);

  lv_style_set_translate_y(&stileBottoniPremuti, 5);
  lv_style_set_shadow_ofs_y(&stileBottoniPremuti, 3);
  lv_style_set_bg_color(&stileBottoniPremuti, lv_palette_darken(LV_PALETTE_GREEN, 2));
  lv_style_set_bg_grad_color(&stileBottoniPremuti, lv_palette_darken(LV_PALETTE_GREEN, 4));

  /*Add a transition to the outline*/
  static lv_style_transition_dsc_t transizioneBottonePremuto;
  static lv_style_prop_t props[] = { LV_STYLE_OUTLINE_WIDTH, LV_STYLE_OUTLINE_OPA };
  lv_style_transition_dsc_init(&transizioneBottonePremuto, props, lv_anim_path_linear, 300, 0, NULL);

  lv_style_set_transition(&stileBottoniPremuti, &transizioneBottonePremuto);

  lv_obj_t *bottone = lv_btn_create(lv_scr_act());
  lv_obj_t *etichetta = lv_label_create(bottone);
  lv_label_set_text(etichetta, nomeBottone);
  lv_obj_center(etichetta);
  lv_obj_remove_style_all(bottone); /*Remove the style coming from the theme*/
  lv_obj_add_style(bottone, &stileBottoni, 0);
  lv_obj_add_style(bottone, &stileBottoniPremuti, LV_STATE_PRESSED);
  lv_obj_set_size(bottone, 100, 100);
  lv_obj_set_pos(bottone, posX, posY);

  return bottone;
}

void callback_pulsante_tipo_acqua(lv_event_t *evento) {
  lv_event_code_t code = lv_event_get_code(evento);
  lv_obj_t *oggetto = lv_event_get_target(evento);
  lv_obj_t *etichetta = lv_obj_get_child(oggetto, 0);
  const char *testo = lv_label_get_text(etichetta);
  //lv_label_set_text(debugLabel, testo);
  uint8_t tipoAcqua;
  uint8_t erogazione;

  if (!bottigliaBicchiere) erogazione = tempoBottiglia;
  else erogazione = tempoBicchiere;

  if (testo == "Liscia") {
    tipoAcqua = 0x00;
  } else if (testo == "Frizzante") {
    tipoAcqua = 0x01;
  } else if (testo == "Liscia calda") {
    tipoAcqua = 0x02;
  }

  uint8_t checksum = 0 - (tipoAcqua + erogazione);

  uint8_t messaggio[4] = { tipoAcqua, erogazione, checksum, 0xED };
  seriale.write(messaggio);
}

void callback_bottiglia(lv_event_t *evento) {
  lv_obj_clear_flag(sliderBottiglia, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(etichettaSliderBottiglia, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_flag(sliderBicchiere, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(etichettaSliderBicchiere, LV_OBJ_FLAG_HIDDEN);

  bottigliaBicchiere = 1;
}

void callback_bicchiere(lv_event_t *evento) {
  lv_obj_clear_flag(sliderBicchiere, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(etichettaSliderBicchiere, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_flag(sliderBottiglia, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(etichettaSliderBottiglia, LV_OBJ_FLAG_HIDDEN);

  bottigliaBicchiere = 0;
}

void callback_sliderBottiglia(lv_event_t *evento) {
  lv_obj_t *slider = lv_event_get_target(evento);
  tempoBottiglia = lv_slider_get_value(slider);
  static char buffer[16];
  snprintf(buffer, sizeof(buffer), "%lu s", tempoBottiglia);
  lv_label_set_text(etichettaSliderBottiglia, buffer);
}

void callback_sliderBicchiere(lv_event_t *evento) {
  lv_obj_t *slider = lv_event_get_target(evento);
  tempoBicchiere = lv_slider_get_value(slider);
  static char buffer[16];
  snprintf(buffer, sizeof(buffer), "%lu s", tempoBicchiere);
  lv_label_set_text(etichettaSliderBicchiere, buffer);
}