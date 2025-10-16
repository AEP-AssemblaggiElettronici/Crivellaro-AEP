/*
  Febbraio 2025 <|==| Fabio Crivellaro |==|>
*/
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_File_Browser.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/fl_ask.H>
#include "bottone_mixer.h"
#include "bottone_banco.h"
#include <iostream>
#include <bitset>
#include <wiringPi.h>
#include <thread>
#include <fstream>
#include <string>
#include <mutex>
#include <condition_variable>
#include <atomic>

#define DIMENSIONE_PULSANTE_MIXER 70
#define DIMENSIONE_PULSANTE_CANALE 50
#define SPAZIO_ORIZZONTALE 30
#define SPAZIO_VERTICALE_1 70
#define SPAZIO_VERTICALE_2 (SPAZIO_VERTICALE_1 + 80)
#define SPAZIO_VERTICALE_3 (SPAZIO_VERTICALE_2 + 80)
#define SPAZIO_VERTICALE_BANCO_1 340
#define SPAZIO_VERTICALE_BANCO_2 (SPAZIO_VERTICALE_BANCO_1 + 50)
#define SPAZIO_VERTICALE_BANCO_3 (SPAZIO_VERTICALE_BANCO_2 + 50)
#define SPAZIO_BOTTONI_BANCO_SINISTRO 10
#define DISTANZA_PULSANTE_MIXER 120
#define NUMERO_BOTTONI 27
#define NUMERO_BOTTONI_CANALI 52

// pin che selezionano la funzione canale del mixer:
#define PIN_CLOCK 3
#define PIN_DATA 5
// pin che selezionano il banco (il canale) del mixer, sono suddivisi
// in 7 banchi da 8 canali ciascuno, i dati vengono inviati tramite
// 3 bit (pin) per il banco e 3 bit per il canale:
#define PIN_CANALE_CANALE_BIT_1 7
#define PIN_CANALE_CANALE_BIT_2 11
#define PIN_CANALE_CANALE_BIT_3 13
#define PIN_CANALE_BANCO_BIT_1 8
#define PIN_CANALE_BANCO_BIT_2 10
#define PIN_CANALE_BANCO_BIT_3 12

#define ESTENSIONE_FILE ".mix"

Fl_Window *finestra = new Fl_Window (1000, 650);
BottoneMixer *bottoni[NUMERO_BOTTONI];
BottoneBanco *bottoniBanco[NUMERO_BOTTONI_CANALI];
Fl_Button *bottoneCarica = new Fl_Button(340, 540, 130, 50, "LOAD");
Fl_Button *bottoneSalva = new Fl_Button(540, 540, 130, 50, "SAVE");

uint32_t canali; // word di stato
bool softwareRunning = 1;
int posizionePrecedente = 0;
static uint32_t bufferCanali[NUMERO_BOTTONI_CANALI] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
std::string titoloSoftware = "MIXER FUNCTIONS ";
const char *etichette[] = { "+48v", "CHAN 0", "MIX 0", "MUTE A", "MUTE B", "SOLO SAFE", "CHAN", "PRE", "CHAN", "PRE", "CHAN", "PRE", "LOW PASS", "HF/LF EQ", "PATCH IN", "MID EQ", "CHAN CUT", "AUX MON", "AUX 1", "AUX 2", "AUX 3", "AUX 4", "AUX A", "AUX B", "MUTE", "SOLO", "___" };
uint8_t bitBanco = 0;
uint8_t bitCanale = 0;

void istanza_clock();
void thread_invia_dati();
void delay_nano(int nano);
void chiusura_software(Fl_Widget* w);
void callback_all_on(Fl_Widget* w);
void callback_all_off(Fl_Widget* w);
void callback_banco(Fl_Widget* w, void* data);
void callback_carica(Fl_Widget* w);
void callback_salva(Fl_Widget* w);
void callback_mixer(Fl_Widget* w, void* data);
void apri_file();
void salva_file();
void errore_file();

int main (int argc, char ** argv)
{
  finestra->label(titoloSoftware.c_str());
  wiringPiSetupPhys(); // numerazione fisica dei pin del rPi (sto sviluppando su un Pi400)
  pinMode(PIN_CLOCK, OUTPUT);
  pinMode(PIN_DATA, OUTPUT);
  pinMode(PIN_CANALE_BANCO_BIT_1, OUTPUT);
  pinMode(PIN_CANALE_BANCO_BIT_2, OUTPUT);
  pinMode(PIN_CANALE_BANCO_BIT_3, OUTPUT);
  pinMode(PIN_CANALE_CANALE_BIT_1, OUTPUT);
  pinMode(PIN_CANALE_CANALE_BIT_2, OUTPUT);
  pinMode(PIN_CANALE_CANALE_BIT_3, OUTPUT);

  std::thread threadClock(istanza_clock); // istanzia i thread del clock continuo e della trasmissione dati

  //std::bitset<32> canali; // DEBUG

  for (int i = 0; i < NUMERO_BOTTONI; i++) // istanziamento bottoni mixer
  {
    if (i < 9)
      bottoni[i] = new BottoneMixer(((i + 1) * DIMENSIONE_PULSANTE_MIXER) + DISTANZA_PULSANTE_MIXER, SPAZIO_VERTICALE_1, DIMENSIONE_PULSANTE_MIXER, DIMENSIONE_PULSANTE_MIXER, etichette[i]);
    else if (i >= 9 && i < 18)
      bottoni[i] = new BottoneMixer((((i - 9) + 1) * DIMENSIONE_PULSANTE_MIXER) + DISTANZA_PULSANTE_MIXER, SPAZIO_VERTICALE_2, DIMENSIONE_PULSANTE_MIXER, DIMENSIONE_PULSANTE_MIXER, etichette[i]);
    else if (i >= 18)
      bottoni[i] = new BottoneMixer((((i - 18) + 1) * DIMENSIONE_PULSANTE_MIXER) + DISTANZA_PULSANTE_MIXER, SPAZIO_VERTICALE_3, DIMENSIONE_PULSANTE_MIXER, DIMENSIONE_PULSANTE_MIXER, etichette[i]);
  }

  for (int i = 0; i < NUMERO_BOTTONI_CANALI; i++) // istanziamento bottoni banco
  {
    if (i < 17)
      bottoniBanco[i] = new BottoneBanco((i + 1) * DIMENSIONE_PULSANTE_CANALE + SPAZIO_BOTTONI_BANCO_SINISTRO, SPAZIO_VERTICALE_BANCO_1, DIMENSIONE_PULSANTE_CANALE, DIMENSIONE_PULSANTE_CANALE, i + 1);
    else if (i >= 17 && i < 34)
      bottoniBanco[i] = new BottoneBanco(((i - 17) + 1) * DIMENSIONE_PULSANTE_CANALE + SPAZIO_BOTTONI_BANCO_SINISTRO, SPAZIO_VERTICALE_BANCO_2, DIMENSIONE_PULSANTE_CANALE, DIMENSIONE_PULSANTE_CANALE, i + 1);
    else if (i >= 34)
      bottoniBanco[i] = new BottoneBanco(((i - 34) + 1) * DIMENSIONE_PULSANTE_CANALE + SPAZIO_BOTTONI_BANCO_SINISTRO, SPAZIO_VERTICALE_BANCO_3, DIMENSIONE_PULSANTE_CANALE, DIMENSIONE_PULSANTE_CANALE, i + 1);
    bottoniBanco[i]->callback(callback_banco, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
  }

  bottoneCarica->callback(callback_carica);
  bottoneSalva->callback(callback_salva);
  // finestra->clear_border(); // non abbiamo bisogno di barra del titolo, l'ho commentato perchè effettivamente serve
  finestra->callback(chiusura_software);
  finestra->end();
  finestra->show(argc, argv);

  while(Fl::wait() && softwareRunning) // loop manuale di FastLightToolKit, si attiva quando non ci sono eventi di FLTK
  {
    for (int i = 0; i < NUMERO_BOTTONI; i++) // aggiornamento word di stato
    {
      if (bottoni[i]->get_on_off())
        canali |= (1 << i);
      else
        canali &= ~(1 << i);
    }
    // std::cout << canali << std::endl; // DEBUG

    // invio dei dati per selezionare il canale sul mixer
    digitalWrite(PIN_CANALE_BANCO_BIT_1, bitBanco & 0x1);
    digitalWrite(PIN_CANALE_BANCO_BIT_2, (bitBanco >> 1) & 0x01);
    digitalWrite(PIN_CANALE_BANCO_BIT_3, (bitBanco >> 2) & 0x01);

    digitalWrite(PIN_CANALE_CANALE_BIT_1, bitCanale & 0x1);
    digitalWrite(PIN_CANALE_CANALE_BIT_2, (bitCanale >> 1) & 0x01);
    digitalWrite(PIN_CANALE_CANALE_BIT_3, (bitCanale >> 2) & 0x01);
  }

  // thread_istanza_clock.join();
  threadClock.join(); // collega al main il thread
  return 0;
}

void istanza_clock()
{
  while(softwareRunning)
  {
    for (int i = -1; i < NUMERO_BOTTONI + 16; i++) // invio dati, il clock è incastrato manualmente nell'invio
    {
      if (i < NUMERO_BOTTONI) // le iterazioni in più saranno sempre considerate 0, per dare il tempo al bit di start
        digitalWrite(PIN_DATA, i != -1 ? (canali >> i) & 1 : 1); // calcolo anche il bit di start se i == -1
      else
        digitalWrite(PIN_DATA, 0);

      delay_nano(1);
      digitalWrite(PIN_CLOCK, 1);
      delay_nano(1);
      digitalWrite(PIN_CLOCK, 0);
      delay_nano(1);
    }
  }
}

void chiusura_software(Fl_Widget* w)
{
  softwareRunning = 0;
  exit(0);
}

void callback_banco(Fl_Widget* w, void* data) // i bottoni per selezionare i banchi (i canali)
{
  int currentPosizione = reinterpret_cast<intptr_t>(data);
  if (currentPosizione != posizionePrecedente)
  {
    for (int i = 0; i < NUMERO_BOTTONI; i++) // pima salviamo lo stato del pulsante precedente
    {
      if (bottoni[i]->get_on_off())
        bufferCanali[posizionePrecedente] |= (1 << i);
      else
        bufferCanali[posizionePrecedente] &= ~(1 << i);

      if ((bufferCanali[currentPosizione] >> i) & 1) // poi richiamiamo lo stato del pulsante premuto
        bottoni[i]->set_on_off(1);
      else
        bottoni[i]->set_on_off(0);
    }

    // aggiornamento grafico dello sondo del bottone canale selezionato
    for (int i = 0; i < NUMERO_BOTTONI_CANALI; i++)
      bottoniBanco[i]->set_selezione(0);

    bottoniBanco[currentPosizione]->set_selezione(1);

    // calcolo della posizione del canale da selezionare
    bitBanco = (currentPosizione / 8) + 1;
    bitCanale = currentPosizione % 8;

    posizionePrecedente = currentPosizione;
  }
}

void callback_carica(Fl_Widget* w)
{
  Fl_Native_File_Chooser sfogliaFile;
  sfogliaFile.title("Seleziona scena da caricare");
  sfogliaFile.type(Fl_Native_File_Chooser::BROWSE_FILE);
  sfogliaFile.filter("Mix\t*.mix\n");

  if (!sfogliaFile.show()) // se l'utente non annulla la selezione file, si procede così
  {
    std::string nomeFile = sfogliaFile.filename();
    std::fstream file(nomeFile);

    if (!file || nomeFile.substr(nomeFile.find('.'), nomeFile.size()) != ".mix")
    {
      fl_alert("Estensione di file non valida!");
      return;
    }

    int conteggioLinee = 0; // controlliamo che il file caricato abbia la giusta intestazione e lunghezza
    std::string linea;
    while (std::getline(file, linea))
    {
      conteggioLinee++;
      if (conteggioLinee > NUMERO_BOTTONI_CANALI + 2 || (conteggioLinee == 1 && linea != "[mixer data]")) // errore se la dimensione dei dati non è valida o la prima riga non ha l'header
      {
        fl_alert("File non valido, file corrotto o non supportato. - Dimensione dato non valido [NR]");
        file.close();
        return;
      }

      if (linea != "[mixer data]")
      {
        /*
         (l'offset di 2 è per mettere il dato correttamente in ogni bottone)
          ->do_callback() prende come argomenti l'oggetto stesso e il parametro da inviare alla callback
          in questo caso serve a selezionare il bottone banco indicato e metterci il dato che si trova
          nella i-esima riga del file
        */
        bottoniBanco[conteggioLinee - 2]->do_callback(bottoniBanco[conteggioLinee - 2], reinterpret_cast<void*>(static_cast<intptr_t>(conteggioLinee - 2)));
        for (int i = 0; i < NUMERO_BOTTONI; i++) // scorre i bottoni del mixer
        {
          if (linea.at(i) == '0' || linea.at(i) == '1') // se i dati nel file sono validi, accende o spegne il bottone corrispondente
          {
            bottoni[i]->set_on_off(linea.at(i) == '1' ? 1 : 0);
          }
          else
          {
            fl_alert("File non valido, file corrotto o non supportato. - Dimensione dato non valido [LR]");
            file.close();
            return;
          }
        }
      }
    }

    if (conteggioLinee != NUMERO_BOTTONI_CANALI + 1)
    {
      fl_alert("File non valido, file corrotto o non supportato. - Dimensione dato non valido [LRC]");
      file.close();
      return;
    }
    finestra->label((titoloSoftware + nomeFile).c_str()); // carica il nome del file nella barra del titolo, .c_str() trasforma la stringa in puntatore a caratteri
    file.close();
    bottoniBanco[0]->do_callback(bottoniBanco[0], reinterpret_cast<void*>(static_cast<intptr_t>(0))); // una volta caricato il file si sposta direttamente al banco 1 (posizione 0)
  }
}

void delay_nano(int nano) // funzione per calcolare in nanosecondi
{
  struct timespec tiempo; // crea una struct definita nella libreria time.h
  tiempo.tv_sec = nano;
  nanosleep(&tiempo, NULL);
}

void callback_salva(Fl_Widget* w)
{
  Fl_Native_File_Chooser sfogliaFile;
  sfogliaFile.title("Seleziona scena da salvare");
  sfogliaFile.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
  sfogliaFile.filter("Mix\t*.mix\n");

  if (sfogliaFile.show() == 0) // se non si annulla il salvataggio si procede così
  {
    std::string nomeFile = sfogliaFile.filename();
    nomeFile.append(ESTENSIONE_FILE);
    std::ofstream file(nomeFile);

    if (!file)
    {
      fl_alert("Errore nella creazione file.");
      return;
    }

    for (int i = 0; i < NUMERO_BOTTONI_CANALI + 1; i++) // scorre i dati dei bottoni banco, poi dei bottoni mixer, e salva i vari stati 1/0 nel file
    {
      if (i == 0)
      {
        file << "[mixer data]" << std::endl;
      }
      else
      {
        bottoniBanco[i - 1]->do_callback(bottoniBanco[i - 1], reinterpret_cast<void*>(static_cast<intptr_t>(i - 1)));
        for (int j = 0; j < NUMERO_BOTTONI; j++)
        {
          file << bottoni[j]->get_on_off();
        }
        if (i != NUMERO_BOTTONI_CANALI)
          file << std::endl;
      }
    }
    finestra->label((titoloSoftware + nomeFile).c_str()); // carica il nome del file nella barra del titolo, .c_str() trasforma la stringa in puntatore a caratteri
    file.close();
  }
}
