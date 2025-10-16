#include "bottone_banco.h"

BottoneBanco::BottoneBanco(int x, int y, int larghezza, int altezza, int posizione)
: Fl_Button(x, y, larghezza, altezza)
{
  BottoneBanco::set_posizione(posizione);
  this->copy_label(convert_int(posizione));
}

void BottoneBanco::set_posizione(int posizione) { this->posizione = posizione; }
int BottoneBanco::get_posizione() { return posizione; }

// funzione che converte in caratteri i decimali presenti sui vari bottoni che selezionano il mutegroup
// viene richiamata nel costruttore
const char* BottoneBanco::convert_int(int num)
{
  buff[0] = (num / 10) + '0';
  buff[1] = (num % 10) + '0';
  buff[2] = '\0';

  return buff;
}
