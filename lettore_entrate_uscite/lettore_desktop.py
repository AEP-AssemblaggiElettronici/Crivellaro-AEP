import tkinter as tk
from tkinter import font as tkfont
import serial
import serial.tools.list_ports
import pycurl
import io
import time
from datetime import datetime
import logging

# == Funzione di convalida ID ==
def convalida(codice):
 if (codice[0:3] != "AEP"):
  return False
 numero = int(codice[4:7])
 if (numero != 1 and numero < 100):
  for i in range(2, numero - 1):
   if (numero % i == 0):
    return False
 else:
  return False
 if codice[8] not in ("E", "U"):
  return False
 return True

# == Trova porta seriale ==
def trova_porta_seriale():
 porte = serial.tools.list_ports.comports()
 for porta in porte:
  if "ttyACM" in porta.device:
   return porta.device
 return None

# == GUI Init ==
root = tk.Tk()
root.geometry("480x320+0+0")
root.overrideredirect(True)
root.title("Lettore QR")

font_grande = tkfont.Font(size=30)
font_piccolo = tkfont.Font(size=24)

label_orologio = tk.Label(root, text="", font=font_piccolo)
label_orologio.pack(pady=10)

label_messaggio = tk.Label(root, text="In attesa codice...", font=font_grande, fg="white", bg="black")
label_messaggio.pack(pady=20)

# == Serial Init ==
porta = trova_porta_seriale()
if not porta:
 print("Nessuna porta trovata")
 exit()

seriale = serial.Serial(porta, 9600, timeout=1)

# == Aggiorna orologio ==
def aggiorna_orologio():
 label_orologio.config(text=datetime.now().strftime("%H:%M:%S"))
 root.after(1000, aggiorna_orologio)

# == Loop di lettura QR ==
dati = ""
def leggi_seriale():
 global dati
 if seriale.in_waiting:
  charr = seriale.read().decode("utf-8")
  dati += charr
  if charr == "\r":
   if dati and convalida(dati):
    richiesta = pycurl.Curl()
    rispostaBuffer = io.BytesIO()
    richiesta.setopt(pycurl.TIMEOUT, 5)
    richiesta.setopt(pycurl.URL, "http://192.168.179.31:1880/stamp?id=" + dati[8:12])
    richiesta.setopt(richiesta.WRITEDATA, rispostaBuffer)
    richiesta.perform()
    richiesta.close()
    rispostaText = rispostaBuffer.getvalue().decode("utf-8")
    if rispostaText == "[]":
     label_messaggio.config(text=dati[13:] + "\n" + ("Entrata" if dati[8] == "E" else "Uscita"), fg="green")
    elif rispostaText == "ERRORE":
     label_messaggio.config(text="CODICE NON VALIDO", fg="red")
    elif rispostaText == "ERRORE ENTRATA":
     label_messaggio.config(text="ENTRATA GIA PRESENTE", fg="yellow")
    else:
     label_messaggio.config(text="ERRORE SCONOSCIUTO", fg="orange")
   else:
    label_messaggio.config(text="CODICE NON VALIDO", fg="red")
   dati = ""
 root.after(100, leggi_seriale)

# == Avvia ==
aggiorna_orologio()
leggi_seriale()
root.mainloop()
