void splitFloat(float number, uint8_t &integerPart, uint8_t &decimalPart) {
  // Estrai la parte intera
  integerPart = static_cast<int>(number);
  // Estrai la parte decimale
  decimalPart = static_cast<int>((number - integerPart) * 10);
}

uint16_t *sht3x(int addr) {
  float tempHum[2] = { 0, 0 };  // 0 temperatura, 1 umidità
  /*
   0 e 2 temperatura, 1 e 3 umidità, con "static" il valore rimane in memoria e possiamo ritornarlo come un puntatore (in questo caso un array)
   0 e 1 sono i valori che verranno trattati con la funzione splitfloat, 2 e 3 vengono inviati con lora
  */
  static uint16_t ritornoValori[4] = { 0, 0, 0, 0 };
  unsigned int byteTemp = 0;
  unsigned int byteHum = 0;
  uint8_t humInt = 0;  // creo due variabili per la parte intera e decimale dell'umidità
  uint8_t humFloat = 0;
  uint8_t tempInt = 0;
  uint8_t tempFloat = 0;

  Serial.println("Invio comandi di lettura SHT31...");  // DEBUG
  Wire.beginTransmission(addr);
  Wire.write(0x2C);  // comandi in byte per leggere temperatura e umidità
  Wire.write(0x06);
  Wire.endTransmission();
  delay(500);

  Serial.println("Comando inviato, scansione temperatura SHT31...");  // DEBUG
  if (Wire.requestFrom(addr, 6) == 6)                                 // legge 6 bytes, i byte 3 e 6 sono valori di checksum
  {
    byteTemp = Wire.read() << 8 | Wire.read();  // i primi due bytes sono il valore della temperatura
    Wire.read();                                // lettura a vuoto perchè quello che legge è un byte di checksum
    byteHum = Wire.read() << 8 | Wire.read();   // il byte 4 e 5 sono quelli del valore di umidità
  } else {
    static uint16_t ritornoNeutro[2];
    ritornoNeutro[0] = 0xFFFE;  // ritorna 255 per temperatura e umidità se non è presente il sensore
    ritornoNeutro[1] = 0xFFFE;
    return ritornoNeutro;
  }

  tempHum[0] = ((-45.0 + 175.0 * ((float)byteTemp / 65535.0)) * 10.0f) / 10;  // Temperatura in °C
  tempHum[1] = ((100.0 * ((float)byteHum / 65535.0)) * 10.0f) / 10;           // Umidità relativa in %

  Serial.print("Valore temperatura: ");
  Serial.print(tempHum[0]);
  Serial.print(" Valore umidità: ");
  Serial.print(tempHum[1]);
  Serial.println();

  // aggiustiamo i valori per inviarli in piattaforma, con una sola cifra decimale
  ritornoValori[2] = (uint16_t)(tempHum[0] * 10);
  ritornoValori[3] = (uint16_t)(tempHum[1] * 10);

  // la funzione splitFloat serve se si deve mandare il dato con la radio sigfox
  splitFloat(tempHum[0], tempInt, tempFloat);
  splitFloat(tempHum[1], humInt, humFloat);

  if (tempFloat >= 8) {         // CASO .8 e .9
    tempInt += 1;               // sale di un grado
    tempInt += 30;              // mantenere valori non negativi
    tempInt *= 2;               // diventa un numero pari
  } else if (tempFloat <= 2) {  // CASO .1 e .2
    tempInt += 30;              // mantenere valori non negativi
    tempInt *= 2;               // diventa un numero pari
  } else {                      // CASO .3, .4, .5, .6,.7
    tempInt += 30;              // mantenere valori non negativi
    tempInt *= 2;               // diventa un numero pari
    tempInt += 1;               // diventa un numero dispari
  }

  ritornoValori[0] = tempInt;

  if (humFloat >= 8) {       // CASO .8 e .9
    humInt += 1;             // sale di un grado
    humInt *= 2;             // diventa un numero pari
  } else if (humFloat <= 2)  // su codice originale leggeva tempFloat, boh!
  {                          // CASO .1 e .2
    humInt *= 2;             // diventa un numero pari
  } else {                   // CASO .3, .4, .5, .6,.7
    humInt *= 2;             // diventa un numero pari
    humInt += 1;             // diventa un numero dispari
  }

  if (humInt >= 1000)
    ritornoValori[1] = 255;
  else
    ritornoValori[1] = humInt;

  return ritornoValori;  // ritorna un puntatore ma che può esser letto come un array
}

bool i2c_scan_sht3x()  // scan I2C per trovare i/l sensori/e
{
  uint8_t error;
  for (int i = 10; i--;) {
    Wire.beginTransmission(0x44);  // 68 è l'indirizzo 0x44 in decimale del sensore I2C
    delay(10);
    error = Wire.endTransmission();
    delay(10);
    if (error == 0) {
      Serial.println("Dispositivo SHT3X trovato");
      return 1;
    }
    delay(10);
  }
  Serial.println("Nessun dispositivo trovato.");
  return 0;
}

/* bool i2c_scan_sht3x()  // scan I2C per trovare i/l sensori/e
{
  byte address, error;
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    // Serial.println(error);  // DEBUG

    if (error == 0) {
      Serial.println(address, HEX);  // DEBUG
      if (address == SHT3X_ADDRESS) {
        Serial.println("Dispositivo SHT3X trovato");
        return 1;
      }
    }
  }
  Serial.println("Nessun dispositivo trovato.");
  return 0;
} */