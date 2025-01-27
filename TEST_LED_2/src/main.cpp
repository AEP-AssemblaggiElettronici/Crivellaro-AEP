#include <Arduino.h>
#include <SoftwareSerial.h>

#define RX_PIN 2
#define TX_PIN 3
#define DEVICE_ID 1        // id del dispositivo, se è quello giusto allora i bytes vengono letti come valori per i led
#define INPUT_OUTPUT_PIN 4 // pin che viene settato I/O per RS485
#define IMG_WIDTH 8
#define IMG_HEIGHT 8
#define CHUNK_SIZE (IMG_WIDTH * 5 * 3)
#define IMAGE_ARRAY_FULL_SIZE (IMG_WIDTH * IMG_HEIGHT * 3)
#define LED_PIN 5
#define BAUD 115200

uint8_t bufferImg[IMAGE_ARRAY_FULL_SIZE]; // larghezza per altezza per RGB (3)
unsigned int indice = 0;
SoftwareSerial RS485(RX_PIN, TX_PIN);

void processaImmagine();

void setup()
{
  Serial.begin(9600);
  RS485.begin(BAUD);
  pinMode(INPUT_OUTPUT_PIN, OUTPUT);
  digitalWrite(INPUT_OUTPUT_PIN, 0);
}

void loop()
{
  if (RS485.available())
  {
    uint8_t header[3];
    if (RS485.readBytes(header, 3) == 3)
    {
      if (header[0] == 0xAA && header[1] == 0xBB)
      {
        if (header[2] == DEVICE_ID) // andiamo avanti solo se l'id dispositivo è quello giusto
        {
          uint8_t buffer[CHUNK_SIZE];
          unsigned int bytesLetti = RS485.readBytes(buffer, CHUNK_SIZE);
          if (bytesLetti == CHUNK_SIZE)
          {
            if (indice + bytesLetti <= sizeof(bufferImg))
            {
              memcpy(&bufferImg[indice], buffer, bytesLetti);
              indice += bytesLetti;
              Serial.print("Bytes letti: ");
              Serial.print(bytesLetti);
              Serial.println();
              Serial.print("Totale letti: ");
              Serial.print(indice);
              Serial.print(" / ");
              Serial.print(sizeof(bufferImg));
              Serial.println();
            }
            else
              Serial.println("Errore nella lettura del buffer!");
          }
          else
            Serial.println("Pacchetto incompleto.");
          if (indice == sizeof(bufferImg))
          {
            Serial.println("Pezzo di immagine ricevuta al 100%");
            processaImmagine();
            indice = 0;
          }
        }
        else
          Serial.println("ID differente.");
      }
      else
        Serial.println("Header non valido.");
    }
  }
}

void processaImmagine()
{
  Serial.println("Elaborazione pezzo di immagine...");
  for (int i = 0; i < IMAGE_ARRAY_FULL_SIZE; i++)
  {
    // qui ci vanno le routine per visualizzare i led a seconda del colore
  }
}