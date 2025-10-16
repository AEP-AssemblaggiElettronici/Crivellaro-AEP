# Inizio maggio 2025 Fabio Crivellaro scribit:

import serial
import serial.tools.list_ports
import pycurl
from urllib.parse import urlencode
import io
import os
import sys
import time
import logging
import spidev as SPI
from datetime import datetime
from PIL import Image, ImageDraw, ImageFont

sys.path.append("..")
sys.path.append("/home/pi/LCD_Module_code/LCD_Module_RPI_code/RaspberryPi/python/")
from lib import LCD_2inch4

# == Trova automaticamente la porta seriale ==
def trova_porta_seriale():
 porte = serial.tools.list_ports.comports()
 for porta in porte:
  if "ttyACM" in porta.device:
   return porta.device
 return None

# == Funzione di convalida ID ==
def convalida(codice):
 #DEBUG
 print(codice) # codice intero
 print(codice[0:3]) # identificativo "AEP"
 print(codice[4:7]) # numero di verifica
 print(codice[8]) # identificativo Entrata o Uscita
 print(codice[9:12]) # identificativo database dipendente
 print(codice[13:]) # nome dipendente da visualizzare
 #DEBUG

 if (codice[0:3] != "AEP"):
  print("Codice non valido")
  return False
 
 numero = int(codice[4:7])
 if (numero != 1 and numero < 100):
  for i in range(2, numero - 1):
   if (numero % i == 0):
    return False
 else:
  return False
 
 if (codice[8] != "E" and codice[8] != "U"):
  return False

 return True 

# == Configurazione ==
logging.basicConfig(level=logging.DEBUG)
endpoint_stamp = "192.168.179.31:1880/stamp?id="
porta_seriale = trova_porta_seriale()

if not porta_seriale:
 print("Errore: Nessuna porta seriale trovata.")
 sys.exit(1)

seriale = serial.Serial(porta_seriale, 9600, timeout=1)

fontVisualizzazione = ImageFont.truetype("/home/pi/LCD_Module_code/LCD_Module_RPI_code/RaspberryPi/python/Font/Font00.ttf", 15)
schermo = LCD_2inch4.LCD_2inch4()
schermo.Init()
schermo.clear()

quadro = Image.new("RGB", (schermo.width, schermo.height), "BLACK")
disegno = ImageDraw.Draw(quadro)

dati = ""
ultimo_aggiornamento_orario = 0
info_visualizzata = None

try:
 # == Inizio loop di programma ==
 while True:
  # == Aggiorna l'orologio ogni secondo ==
  ora_corrente = time.time()
  if ora_corrente - ultimo_aggiornamento_orario >= 1:
   quadro = Image.new("RGB", (schermo.width, schermo.height), "BLACK")
   disegno = ImageDraw.Draw(quadro)
   orario = datetime.now().strftime("%H:%M:%S")
   disegno.text((5, 5), orario, fill="WHITE", font=fontVisualizzazione)

   # == Se c'è un messaggio visualizzato, ridisegnalo sotto l'orologio ==
   if info_visualizzata:
    disegno.text((5, 160), info_visualizzata[0], fill=info_visualizzata[1], font=fontVisualizzazione)
    if len(info_visualizzata) > 2:
     disegno.text((5, 180), info_visualizzata[2], fill=info_visualizzata[1], font=fontVisualizzazione)

   schermo.ShowImage(quadro)
   ultimo_aggiornamento_orario = ora_corrente

  # == Legge caratteri dalla seriale ==
  charr = seriale.read().decode("utf-8")
  dati += charr

  if charr == "\r":  # codice terminato
   print("Codice inserito") # DEBUG
   if dati and convalida(dati):
    print("Richiesta al server") # DEBUG
    richiesta = pycurl.Curl()
    rispostaBuffer = io.BytesIO()
    richiesta.setopt(pycurl.TIMEOUT, 5)
    richiesta.setopt(pycurl.URL, endpoint_stamp + dati[8:12])
    richiesta.setopt(richiesta.WRITEDATA, rispostaBuffer)
    richiesta.perform()
    richiesta.close()
    rispostaText = rispostaBuffer.getvalue().decode("utf-8")

    if rispostaText == "[]":
     print("Codice valido:", dati)
     info_visualizzata = [dati[13:], "GREEN", "Entrata" if dati[8] == 'E' else "Uscita"]
    elif rispostaText == "ERRORE":
     print("Codice non valido:", dati)
     info_visualizzata = ["CODICE NON VALIDO", "RED", dati.strip()]
    elif rispostaText == "ERRORE ENTRATA":
     print("Entrata già presente:", dati)
     info_visualizzata = ["ENTRATA GIA PRESENTE", "YELLOW"]
    else:
     print("Errore sconosciuto:", rispostaText)
     info_visualizzata = ["ERRORE", "RED"]
   else:
    info_visualizzata = ["CODICE NON VALIDO", "RED"]

   dati = ""

except IOError as e:
 logging.info(e)
except KeyboardInterrupt:
 schermo.module_exit()
 logging.info("Quit")
 exit()
