void rs485_estesa(HardwareSerial &device, uint8_t array[], const uint8_t comando[]) {
  // pulizia buffer prima della nuova lettura
  while (device.available()) device.read();

  digitalWrite(RS485_DE, 1);
  digitalWrite(RS485_RE, 1);
  delay(5);

  device.write(comando, 8);
  device.flush();
  delay(5);

  digitalWrite(RS485_RE, 0);
  digitalWrite(RS485_DE, 0);

  unsigned long start = millis();
  int idx = 0;

  while (millis() - start < 1000) {
    if (device.available()) {
      array[idx++] = device.read();
      if (idx >= 19) break;
      delay(10);
    }
  }

  if (idx < 7) {
    Serial.println("⚠️ Nessuna risposta RS485");
    for (int i = 0; i < 19; i++) array[i] = 0xFF;
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