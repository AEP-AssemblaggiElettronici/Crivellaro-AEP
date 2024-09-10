#include <Arduino.h>
#include <U8g2lib.h>

#define IN1 39 // pin ingressi voltaggio
#define IN2 34
#define IN3 35
#define SCL 19 // pin per i²c
#define SDA 23

const int range[3][2] = {{400, 490}, {585, 670}, {1840, 1920}};                                             // range di voltaggi per ogni ingresso: {range basso, range alto}
int v[3];                                                                                                   // valori tensione in ingresso
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE, /* clock=*/SCL, /* data=*/SDA); // crea oggetto per usare lo schermo OLED (ci sono i pin dell'I²C)
String righe[3];                                                                                            // righe con i valori da visualizzare

float map_float(int, int, int, int, int);

void setup()
{
  pinMode(IN1, INPUT);
  pinMode(IN2, INPUT);
  pinMode(IN3, INPUT);

  u8g2.begin();
}

void loop()
{
  u8g2.clearBuffer();                 // pulisci il buffer
  u8g2.setFont(u8g2_font_12x6LED_tr); // scegli font

  v[0] = analogRead(IN2); // prende in valori delle tensioni in ingresso
  v[1] = analogRead(IN3);
  v[2] = analogRead(IN1);

  for (int i = 0; i < sizeof(v) / sizeof(int); i++)
  {
    righe[i] = String(i + 1) + " :: " + (String)(v[i] != 0 ? map_float(v[i], 0, 4095, 0, 20) + 0.55 : 0.0) + " V"; // formattazione riga

    u8g2.setCursor(15, (i + 1) * 20); // posiziona cursore
    u8g2.print(righe[i]);             // stampa valore
    // visualizza le condizioni di range:
    u8g2.drawButtonUTF8(97, (i + 1) * 20, v[i] < range[i][1] && v[i] > range[i][0] ? U8G2_BTN_BW1 : U8G2_BTN_INV, 10, 10, 2, v[i] < range[i][1] && v[i] > range[i][0] ? "|OK|" : "||||");
  }

  u8g2.sendBuffer(); // invia buffer
  delay(50);
}

float map_float(int x, int in_min, int in_max, int out_min, int out_max)
{
  return (float)(x - in_min) * (out_max - out_min) / (float)(in_max - in_min) + out_min;
}