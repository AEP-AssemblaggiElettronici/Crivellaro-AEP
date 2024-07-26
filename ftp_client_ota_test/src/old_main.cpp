#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <Update.h>
#include <FTPduino.h>

#define SSID "HOTSPOT_TEST"
#define PASS "hotspot_test"

const char *binaryFileName = "/pompapozzo.bin";

const char *ftp_server = "192.168.8.156";
const unsigned short int ftp_port = 2121;
const char *ftp_user = "ftp";
const char *ftp_pass = "ftp";

FTPduino ftp;

int *versionGrabber(String);
void updateVersion();
bool connect_to_FTP();

void setup()
{
  Serial.begin(9600);
  WiFi.begin(SSID, PASS);
  Serial.println("Attendere connessione WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(250);
  }
  Serial.println("WiFi connesso all'indirizzo IP:");
  Serial.println(WiFi.localIP());
}

void loop()
{
  // Serial.println(WiFi.localIP());
  if (Serial.read() == 'u')
    updateVersion();
}

void updateVersion()
{
  Serial.println("Aggiornamento versione");
  connect_to_FTP();
  size_t bufferSize = ftp.getFileSize(binaryFileName);

  Serial.printf("Total heap: %d\n", ESP.getHeapSize());
  Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
  Serial.printf("Total PSRAM: %d\n", ESP.getPsramSize());
  Serial.printf("Free PSRAM: %d\n", ESP.getFreePsram());
  Serial.printf("Firmware to update size: %d\n", bufferSize);
  if (bufferSize > ESP.getFreeHeap())
  {
    Serial.println("Memoria insufficiente, comprare un ESP32 con maggiore RAM");
    return;
  }

  uint8_t fileBuffer[bufferSize];
  Serial.println(sizeof(fileBuffer)); // qui il firmware si resetta, problemi di tipo forse? boh!
  if (ftp.downloadFile(binaryFileName, fileBuffer, bufferSize))
  {
    Serial.println("Versione recente scaricata, inizio dell'aggiornamento firmware");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
    {
      Serial.println("Errore all'inizio dell'upload nuova versione");
      ftp.disconnect();
      return;
    }
    Update.write(fileBuffer, bufferSize); // !!UPDATE!!
    if (Update.isRunning())
      ;
    if (Update.end())
    {
      Serial.println("Fine dell'udpate, riavvio");
      ftp.disconnect();
      delay(3000);
      ESP.restart();
    }
    else
    {
      Serial.println("Upload fallito alla fine");
      ftp.disconnect();
      return;
    }
  }
}

bool connect_to_FTP()
{
  Serial.println("Connessione al server FTP " + (String)ftp_server);
  if (ftp.connect(ftp_server, ftp_port))
  {
    Serial.println("Connessi al server FTP");
    if (ftp.authenticate(ftp_user, ftp_pass))
    {
      ftp.setWorkDirectory("/");
      return 1;
    }
    Serial.println("FTP: Nome utente o password errati");
  }
  Serial.println("FTP: Connessione fallita");
  return 0;
}