#include <Arduino.h>
#include <U8g2lib.h>

#define IN1 39
#define IN2 34
#define IN3 35
#define BAUD 9600
#define VOLT_CONST 0.0065
#define SCL 19
#define SDA 23

const int rangeAlti[3] = {490, 670, 1920};
const int rangeBassi[3] = {400, 585, 1880};
int v[3];                                                                                                   // valori tensione in ingresso
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE, /* clock=*/SCL, /* data=*/SDA); // ESP32 Thing, HW I2C with pin remapping
String righe[3];                                                                                            // righe con i valori da visualizzare

void setup()
{
  pinMode(IN1, INPUT);
  pinMode(IN2, INPUT);
  pinMode(IN3, INPUT);

  Serial.begin(BAUD);
  u8g2.begin();
}

void loop()
{
  u8g2.clearBuffer();                 // pulisci il buffer
  u8g2.setFont(u8g2_font_12x6LED_tr); // scegli font

  v[0] = analogRead(IN1); // prende in valori delle tensioni in ingresso
  v[1] = analogRead(IN2);
  v[2] = analogRead(IN3);
  // printf("____________\nV1: %d\nV2: %d\nV3: %d\n____________\n", v[0], v[1], v[2]);
  for (int i = 0; i < sizeof(v) / sizeof(int); i++)
  {
    righe[i] = String(i + 1) + " :: " + (String)(v[i] * VOLT_CONST) + " V";

    u8g2.setCursor(15, (i + 1) * 20); // posiziona cursore
    u8g2.print(righe[i]);             // stampa valore
    // u8g2.setCursor(64, i * 10);
    // visualizza le condizioni di range:
    u8g2.drawButtonUTF8(97, (i + 1) * 20, v[i] < rangeAlti[i] && v[i] > rangeBassi[i] ? U8G2_BTN_BW1 : U8G2_BTN_INV, 10, 10, 2, v[i] < rangeAlti[i] && v[i] > rangeBassi[i] ? "|OK|" : "||||");
  }

  u8g2.sendBuffer(); // invia buffer
  delay(50);
}