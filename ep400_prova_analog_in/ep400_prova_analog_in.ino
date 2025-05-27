#define ANALOG_IN1 A0  // Pin input analogici dei sensori
#define ANALOG_IN2 A1
#define ANALOG_IN3 A2

void analog_average(int ch, int AN_ch) {  // lettura sensori analogici con relativa media, se il sensore non è presente, ritorna un valore fisso
  if (!sensorsPresence[ch]) {
    Analog_Value[ch] = analogRead(AN_ch);
    delayMicroseconds(1000);
    Analog_Value[ch] = analogRead(AN_ch);
    Analog_Value[ch] = 0;
    delayMicroseconds(1000);

    for (int i = 0; i < 1000; i++) {
      Analog_Value[ch] += analogRead(AN_ch);
      delayMicroseconds(10);
    }

    Analog_Value[ch] /= 1000;
    wdt_reset();
  } else
    Analog_Value[ch] = 1024;
}

void setup() {
  pinMode(ANALOG_IN1, INPUT);
  pinMode(ANALOG_IN2, INPUT);
  pinMode(ANALOG_IN3, INPUT);

  Serial.begin(115200);
}

void loop() {
  Serial.println(analogRead(ANALOG_IN1));
  Serial.println(analogRead(ANALOG_IN2));
  Serial.println(analogRead(ANALOG_IN3));
  Serial.println("::::::.");
  delay(1000);
}
