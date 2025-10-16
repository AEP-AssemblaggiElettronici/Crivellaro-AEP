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
#include <time.h>
#include <fstream>
#include <string>

#define DIMENSIONE_PULSANTE_MIXER 50
#define SPAZIO_ORIZZONTALE 30
#define SPAZIO_VERTICALE_1 70
#define SPAZIO_VERTICALE_2 (SPAZIO_VERTICALE_1 + 80)
#define SPAZIO_VERTICALE_BANCO_1 (SPAZIO_VERTICALE_2 + 80)
#define SPAZIO_VERTICALE_BANCO_2 (SPAZIO_VERTICALE_BANCO_1 + 80)
#define NUMERO_BOTTONI 32

#define PIN_CLOCK 3 // i numeri di questi tre pin di output
#define PIN_DATA 5
#define PIN_DATA_ENABLE 7

#define ESTENSIONE_FILE ".mix"

Fl_Window *finestra = new Fl_Window (1150, 400);
BottoneMixer *bottoni[NUMERO_BOTTONI];
BottoneBanco *bottoniBanco[NUMERO_BOTTONI];
Fl_Button *bottoneAzzera = new Fl_Button(1000, SPAZIO_VERTICALE_1, 100, 50, "Mute All");
Fl_Button *bottoneSblocca = new Fl_Button(1000, SPAZIO_VERTICALE_2, 100, 50, "Unmute All");
Fl_Button *bottoneCarica = new Fl_Button(1000, SPAZIO_VERTICALE_BANCO_1, 100, 50, "LOAD");
Fl_Button *bottoneSalva = new Fl_Button(1000, SPAZIO_VERTICALE_BANCO_2, 100, 50, "SAVE");

uint32_t canali; // word di stato
bool softwareRunning = 1;
int posizionePrecedente = 0;
static uint32_t bufferCanali[32] = {0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
std::string titoloSoftware = "MIXER MUTE GROUPS ";

void istanza_clock();
void delay_nano(int nano);
void chiusura_software(Fl_Widget* w);
void callback_all_on(Fl_Widget* w);
void callback_all_off(Fl_Widget* w);
void callback_banco(Fl_Widget* w, void* data);
void callback_carica(Fl_Widget* w);
void callback_salva(Fl_Widget* w);
void apri_file();
void salva_file();
void errore_file();

int main (int argc, char ** argv)
{
  finestra->label(titoloSoftware.c_str());
  wiringPiSetupPhys(); // numerazione fisica dei pin del rPi (sto sviluppando su un Pi400)
  pinMode(PIN_CLOCK, OUTPUT);
  pinMode(PIN_DATA, OUTPUT);
  pinMode(PIN_DATA_ENABLE, OUTPUT);

  std::thread thread_istanza_clock(istanza_clock);

  // std::bitset<32> canali; // DEBUG

  for (int i = 0; i < NUMERO_BOTTONI; i++) // istanziamento bottoni mixer
  {
    bottoni[i] = i < 16 ?
      new BottoneMixer((i + 1) * DIMENSIONE_PULSANTE_MIXER, SPAZIO_VERTICALE_1, DIMENSIONE_PULSANTE_MIXER, DIMENSIONE_PULSANTE_MIXER) :
      new BottoneMixer(((i - 16) + 1) * DIMENSIONE_PULSANTE_MIXER, SPAZIO_VERTICALE_2, DIMENSIONE_PULSANTE_MIXER, DIMENSIONE_PULSANTE_MIXER);
  }

  for (int i = 0; i < NUMERO_BOTTONI; i++) // istanziamento bottoni banco
  {
    bottoniBanco[i] = i < 16 ?
      new BottoneBanco((i + 1) * DIMENSIONE_PULSANTE_MIXER, SPAZIO_VERTICALE_BANCO_1, DIMENSIONE_PULSANTE_MIXER, DIMENSIONE_PULSANTE_MIXER, i + 1) :
      new BottoneBanco(((i - 16) + 1) * DIMENSIONE_PULSANTE_MIXER, SPAZIO_VERTICALE_BANCO_2, DIMENSIONE_PULSANTE_MIXER, DIMENSIONE_PULSANTE_MIXER, i + 1);
      bottoniBanco[i]->callback(callback_banco, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
  }

  bottoneAzzera->callback(callback_all_off);
  bottoneSblocca->callback(callback_all_on);
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
  }

  thread_istanza_clock.join();
  return 0;
}

void delay_nano(int nano) // funzione per calcolare in nanosecondi
{
  struct timespec tiempo; // crea una struct definita nella libreria time.h
  tiempo.tv_sec = nano;
  nanosleep(&tiempo, NULL);
}

void istanza_clock()
{
  while(softwareRunning)
  {
    digitalWrite(PIN_DATA_ENABLE, 0);
//    delay_nano(100);
    for (int i = -1; i < NUMERO_BOTTONI + 8; i++) // invio dati, il clock è incastrato manualmente nell'invio
    {
      if (i < NUMERO_BOTTONI) // le iterazioni in più saranno sempre considerate 0, per dare il tempo al bit di start
        digitalWrite(PIN_DATA, i != -1 ? (canali << i) & 1 : 1); // calcolo anche il bit di start se i == -1
      else
        digitalWrite(PIN_DATA, 0);

      delay_nano(300);
      digitalWrite(PIN_CLOCK, 1);
      delay_nano(600);
      digitalWrite(PIN_CLOCK, 0);
      delay_nano(300);
    }
//    digitalWrite(PIN_DATA_ENABLE, 1);
  }
}

void chiusura_software(Fl_Widget* w)
{
  softwareRunning = 0;
  exit(0);
}

void callback_all_off(Fl_Widget* w)
{
  for (int i = 0; i < NUMERO_BOTTONI; i++)
    bottoni[i]->set_on_off(0);
}

void callback_all_on(Fl_Widget* w)
{
  for (int i = 0; i < NUMERO_BOTTONI; i++)
    bottoni[i]->set_on_off(1);
}

void callback_banco(Fl_Widget* w, void* data) // i bottoni per selezionare i banchi di mutegroups
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
      fl_alert("File non valido!");
      return;
    }

    int conteggioLinee = 0; // controlliamo che il file caricato abbia la giusta intestazione e lunghezza
    std::string linea;
    while (std::getline(file, linea))
    {
      conteggioLinee++;
      if (conteggioLinee > 33 || (conteggioLinee == 1 && linea != "[mixer data]"))
      {
        fl_alert("File non valido, file corrotto o non supportato.");
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
        for (int i = 0; i < 32; i++)
        {
          if (linea.at(i) == '0' || linea.at(i) == '1')
          {
            bottoni[i]->set_on_off(linea.at(i) == '1' ? 1 : 0);
          }
          else
          {
            fl_alert("File non valido, file corrotto o non supportato.");
            file.close();
            return;
          }
        }
      }
    }
    if (conteggioLinee != 33)
    {
      fl_alert("File non valido, file corrotto o non supportato.");
      file.close();
      return;
    }
    finestra->label((titoloSoftware + nomeFile).c_str()); // carica il nome del file nella barra del titolo, .c_str() trasforma la stringa in puntatore a caratteri
    file.close();
    bottoniBanco[0]->do_callback(bottoniBanco[0], reinterpret_cast<void*>(static_cast<intptr_t>(0))); // una volta caricato il file si sposta direttamente al banco 1 (posizione 0)
  }
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

    for (int i = 0; i < 33; i++)
    {
      if (i == 0)
      {
        file << "[mixer data]" << std::endl;
      }
      else
      {
        bottoniBanco[i - 1]->do_callback(bottoniBanco[i - 1], reinterpret_cast<void*>(static_cast<intptr_t>(i - 1)));
        for (int j = 0; j < 32; j++)
        {
          file << bottoni[j]->get_on_off();
        }
        if (i != 32)
          file << std::endl;
      }
    }
    finestra->label((titoloSoftware + nomeFile).c_str()); // carica il nome del file nella barra del titolo, .c_str() trasforma la stringa in puntatore a caratteri
    file.close();
  }
}
