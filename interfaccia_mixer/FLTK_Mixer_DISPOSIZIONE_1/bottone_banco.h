#ifndef BOTTONE_BANCO_H_INCLUDED
#define BOTTONE_BANCO_H_INCLUDED

#include <Fl/Fl_Button.H>
#include "bottone_mixer.h"

class BottoneBanco : public Fl_Button
{
  private:
    int posizione;
    char buff[3];

  public:
    BottoneBanco(int x, int y, int larghezza, int altezza, int posizione);
    void set_posizione(int posizione);
    int get_posizione();
    const char* convert_int(int num);
};

#endif // BOTTONE_BANCO_H_INCLUDED
