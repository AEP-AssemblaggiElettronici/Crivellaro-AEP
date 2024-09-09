#include <Arduino.h>
#include <U8x8lib.h>

#define IN1 39
#define IN2 34
#define IN3 35
#define BAUD 9600
#define VOLT_CONST 0.0065
#define V1_RANGE_LO 400
#define V1_RANGE_HI 490
#define V2_RANGE_LO 585
#define V2_RANGE_HI 670
#define V3_RANGE_LO 1880
#define V3_RANGE_HI 1920

int v[3];

void setup()
{
  pinMode(IN1, INPUT);
  pinMode(IN2, INPUT);
  pinMode(IN3, INPUT);

  Serial.begin(BAUD);
}

void loop()
{
  v[0] = analogRead(IN1);
  v[1] = analogRead(IN2);
  v[2] = analogRead(IN3);
  printf("____________\nV1: %d\nV2: %d\nV3: %d\n____________\n", v[0], v[1], v[2]);
  printf("V1: %.3f V (approssimato) - ", v[0] * VOLT_CONST);
  printf(v[0] < V1_RANGE_HI && v[0] > V1_RANGE_LO ? " Valore OK\n" : " Valore fuori scala\n");

  printf("V2: %.3f V (approssimato) - ", v[1] * VOLT_CONST);
  printf(v[1] < V2_RANGE_HI && v[1] > V2_RANGE_LO ? " Valore OK\n" : " Valore fuori scala\n");

  printf("V3: %.3f V (approssimato) - ", v[2] * VOLT_CONST);
  printf(v[2] < V3_RANGE_HI && v[2] > V3_RANGE_LO ? " Valore OK\n" : " Valore fuori scala\n");

  delay(1000);
}