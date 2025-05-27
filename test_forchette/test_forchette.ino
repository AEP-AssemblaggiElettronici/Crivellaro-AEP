#define FORK A2 // PORTA D SU SCHEDE EP392
#define BOOST_SHTDWN 9 // 12v per alimentazione sensoristica analogica (D e C)
#define BOOST_EN 3     // alimentazione sulle porte D e C

void setup() {
  pinMode(FORK, INPUT);
  pinMode(BOOST_EN, OUTPUT);
  pinMode(BOOST_SHTDWN, OUTPUT);
  digitalWrite(BOOST_SHTDWN, 1);
  digitalWrite(BOOST_EN, 1);
  Serial.begin(115200);
}

void loop() {
  int value = 0;
  for (int i = 0; i < 10; i++)
    value += analogRead(FORK);
  Serial.println(value / 10);
  delay(500);
}
