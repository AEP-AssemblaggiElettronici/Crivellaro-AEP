#include <Arduino.h>         // required before wiring_private.h
#include "wiring_private.h"  // pinPeripheral() function

Uart radio(&sercom1, 12, 10, SERCOM_RX_PAD_3, UART_TX_PAD_2);

void SERCOM1_Handler() {
  radio.IrqHandler();
}

void setup() {
  Serial.begin(115200);
  Serial.println("radio test");

  // Assign pins 10 & 11 SERCOM functionality
  pinPeripheral(10, PIO_SERCOM);
  pinPeripheral(12, PIO_SERCOM);
}

// echo back and forth
void loop() {
  uint8_t msgL[70];
  // I primi 6 bytes contengono i caratteri dell'ID LORA
  msgL[0] = 'L';
  msgL[1] = '0';
  msgL[2] = '0';
  msgL[3] = '0';
  msgL[4] = '0';
  msgL[5] = 'X';
  //---------------------------------- A01         (SHT31 Temperatura 1)
  msgL[6] = 0xAA;
  msgL[7] = 0xBB;
  //---------------------------------- A02         (SHT31 Umidità 1)
  msgL[8] = 0xCC;
  msgL[9] = 0xDD;
  //---------------------------------- A03         (Luxmetro 1)
  msgL[10] = 0x0D;
  msgL[11] = 0x0E;
  //---------------------------------- A04         (SENSORE DALLAS 1)
  msgL[12] = 0x0A;
  msgL[13] = 0x0D;
  //---------------------------------- A05
  msgL[14] = 0xFF;
  msgL[15] = 0xFE;
  //---------------------------------- A06
  msgL[16] = 0xFF;
  msgL[17] = 0xFE;
  //---------------------------------- B07         (SHT31 Temperatura 2)
  msgL[18] = 0xFF;
  msgL[19] = 0xFE;
  //---------------------------------- B08         (SHT31 Umidità 2)
  msgL[20] = 0xFF;
  msgL[21] = 0xFE;
  //---------------------------------- B09         (Luxmetro 2)
  msgL[22] = 0xFF;
  msgL[23] = 0xFE;
  //---------------------------------- B10         (SENSORE DALLAS 2)
  msgL[24] = 0xFF;
  msgL[25] = 0xFE;
  //---------------------------------- B11         (Pluviometro)
  msgL[26] = 0xFF;
  msgL[27] = 0xFE;
  //---------------------------------- B12         (Drenato)
  msgL[28] = 0xFF;
  msgL[29] = 0xFE;
  //---------------------------------- C13         (Forchetta umidità 1)
  msgL[30] = 0xFF;
  msgL[31] = 0xFE;
  //---------------------------------- C14         (SENSORE DALLAS 3)
  msgL[32] = 0xFF;
  msgL[33] = 0xFE;
  //---------------------------------- C15         (Anemometro)
  msgL[34] = 0x11;
  msgL[35] = 0x22;
  //---------------------------------- C16
  msgL[36] = 0x33;
  msgL[37] = 0x44;
  //---------------------------------- C17
  msgL[38] = 0x55;
  msgL[39] = 0x66;
  //---------------------------------- C18
  msgL[40] = 0x77;
  msgL[41] = 0x88;
  //---------------------------------- C19
  msgL[42] = 0x99;
  msgL[43] = 0x10;
  //---------------------------------- C20         (Sensore peso 1)
  msgL[44] = 0x11;
  msgL[45] = 0x12;
  //---------------------------------- C21
  msgL[46] = 0x13;
  msgL[47] = 0x14;
  //---------------------------------- C22
  msgL[48] = 0x15;
  msgL[49] = 0x16;
  //---------------------------------- D23         (FORCHETTA umidità 2)
  msgL[50] = 0x17;
  msgL[51] = 0x18;
  //---------------------------------- D24         (SENSORE DALLAS 4)
  msgL[52] = 0x19;
  msgL[53] = 0x20;
  //---------------------------------- D25         (Segnavento)
  msgL[54] = 0x21;
  msgL[55] = 0x22;
  //---------------------------------- D26         (Sensore peso 2)
  msgL[56] = 0x23;
  msgL[57] = 0x24;
  //---------------------------------- D27
  msgL[58] = 0x25;
  msgL[59] = 0x26;
  //---------------------------------- D28
  msgL[60] = 0x27;
  msgL[61] = 0x28;
  //---------------------------------- D29           (CICLI del firmware dall'accensione)
  msgL[62] = 0x29;
  msgL[63] = 0x30;
  //---------------------------------- BAT30         (BATTERIA)
  msgL[64] = 0xFF;
  msgL[65] = 0xFE;
  //----------------------------------- RC
  msgL[66] = 50;  // sostituire con valore randomico
  //------------------------------ END
  msgL[67] = 0;
  msgL[68] = 255;
  msgL[69] = 255;

  radio.begin(1200);
  for (int i = 0; i < 70; i++) {
    radio.write(msgL[i]);
  }
  Serial.println("MESSAGGIO INVIATO");
  radio.end();

  delay(5000);
}
