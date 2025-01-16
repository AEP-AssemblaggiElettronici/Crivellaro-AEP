#include <Arduino.h>
#include <U8g2lib.h>

#define IN1 39 // pin ingressi voltaggio
#define IN2 34
#define IN3 35
#define SCL 19 // pin per i²c
#define SDA 23
#define APPROSSIMAZIONE 0.55
#define VALORE_MASSIMO_MAP 20

const int range[3][2] = {{400, 490}, {585, 670}, {1880 /* 1840 */, 1920}};                                  // range di voltaggi per ogni ingresso: {range basso, range alto}
int v[3];                                                                                                   // valori tensione in ingresso
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE, /* clock=*/SCL, /* data=*/SDA); // crea oggetto per usare lo schermo OLED (ci sono i pin dell'I²C)
String righe[3] = {
    "2.7v - 3.1v",
    "3.5v - 3.9v",
    "9.5v - 9.9v"}; // righe con i valori da visualizzare
short int x, y, x_, y_ = 0;
short int x1, x1_ = u8g2.getDisplayWidth() - 5;
short int y1, y1_ = u8g2.getDisplayHeight() - 1;
const short int verso = 3;
const short int verso1 = -3;
// char direction = 'r';
bool direction = 0;

float map_float(int, int, int, int, int);
void line_stuff();

void setup()
{
  pinMode(IN1, INPUT);
  pinMode(IN2, INPUT);
  pinMode(IN3, INPUT);

  u8g2.begin();
  Serial.begin(9600);
}

void loop()
{
  u8g2.clearBuffer();                 // pulisci il buffer
  u8g2.setFont(u8g2_font_12x6LED_tr); // scegli font

  line_stuff();

  v[0] = analogRead(IN2); // prende in valori delle tensioni in ingresso
  v[1] = analogRead(IN3);
  v[2] = analogRead(IN1);

  for (int i = 0; i < sizeof(v) / sizeof(int); i++)
  {
    // righe[i] = String(i + 1) + " :: " + (String)(v[i] != 0 ? map_float(v[i], 0, 4095, 0, VALORE_MASSIMO_MAP) + APPROSSIMAZIONE : 0.0) + " V"; // formattazione riga

    u8g2.setCursor(15, (i + 1) * 20); // posiziona cursore
    u8g2.print(righe[i]);             // stampa valore
    // visualizza le condizioni di range:
    u8g2.drawButtonUTF8(97, (i + 1) * 20, v[i] < range[i][1] && v[i] > range[i][0] ? U8G2_BTN_INV : U8G2_BTN_BW0, 10, 10, 2, v[i] < range[i][1] && v[i] > range[i][0] ? "OK" : "||||");
  }

  u8g2.sendBuffer(); // invia buffer
  delay(50);
}

float map_float(int x, int in_min, int in_max, int out_min, int out_max)
{
  return (float)(x - in_min) * (out_max - out_min) / (float)(in_max - in_min) + out_min;
}

void line_stuff()
{
  if (direction)
  {
    if (x_ != u8g2.getDisplayWidth() - 5)
      x_ += verso;
    else
    {
      x += verso;
      if (x == u8g2.getDisplayWidth() - 5)
        direction = !direction;
    }
    if (x1_ != 0)
      x1_ -= verso;
    else
    {
      x1 -= verso;
    }
  }
}

/* void line_stuff()
{
  switch (direction)
  {
  case 'r':
    if (x_ != u8g2.getDisplayWidth() - 5)
      x_ += verso;
    else
    {
      x += verso;
      if (x == u8g2.getDisplayWidth() - 5)
        direction = 'd';
    }
    break;
  case 'd':
    if (y_ != u8g2.getDisplayHeight() - 1)
      y_ += verso;
    else
    {
      y += verso;
      if (y == u8g2.getDisplayHeight() - 1)
        direction = 'l';
    }
    break;
  case 'l':
    if (x_ != 0)
      x_ -= verso;
    else
    {
      x -= verso;
      if (x == 0)
        direction = 'u';
    }
    break;
  case 'u':
    if (y_ != 0)
      y_ -= verso;
    else
    {
      y -= verso;
      if (y == 0)
        direction = 'r';
    }
    break;
  }

  u8g2.drawLine(x, y, x_, y_);
} */