/* void rs485(HardwareSerial &device, uint8_t array[]) {
  digitalWrite(RS485_DE, 1);
  digitalWrite(RS485_RE, 1);
  delay(250);
  device.write(umiditaTemperatura, 8);
  device.flush();
  digitalWrite(RS485_RE, 0);
  digitalWrite(RS485_DE, 0);
  delay(10);

  if (device.available() > 0) {
    for (int i = 0; i < 11; i++) {
      array[i] = device.read();
      Serial.print(array[i], HEX);
      Serial.print("|");
    }
    Serial.println();
    Serial.print("Temperatura rs485: ");
    Serial.println((array[5] << 8) | array[6], HEX);
    Serial.println(((array[5] << 8) | array[6]) / 10);
    Serial.print("Umidità rs485: ");
    Serial.println((array[3] << 8) | array[4], HEX);
    Serial.println(((array[3] << 8) | array[4]) / 10);
    return;
  } else {
    Serial.println("Sensore temperatura/umidità terreno RS485 non disponibile");
    return;
  }
} */

void rs485(HardwareSerial &device, uint8_t array[]) {
  // pulizia buffer prima della nuova lettura
  while (device.available()) device.read();

  digitalWrite(RS485_DE, 1);
  digitalWrite(RS485_RE, 1);
  delay(5);

  device.write(umiditaTemperatura, 8);
  device.flush();

  digitalWrite(RS485_RE, 0);
  digitalWrite(RS485_DE, 0);

  unsigned long start = millis();
  int idx = 0;

  // attendi fino a 300 ms per la risposta
  while (millis() - start < 300) {
    if (device.available()) {
      array[idx++] = device.read();
      if (idx >= 11) break;
    }
  }

  if (idx < 7) {
    Serial.println("⚠️ Nessuna risposta RS485");
    for (int i = 0; i < 11; i++) array[i] = 0xFF;
  } else {
    Serial.print("RX [");
    for (int i = 0; i < idx; i++) {
      Serial.print(array[i], HEX);
      Serial.print("|");
    }
    Serial.println("]");
  }

  // tempo di quiete prima della prossima query
  delay(200);
}