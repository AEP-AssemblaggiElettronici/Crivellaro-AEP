#ifndef BOTTONE_MIXER_H_INCLUDED
#define BOTTONE_MIXER_H_INCLUDED

#include <Fl/Fl_Box.H>

class BottoneMixer : public Fl_Box
{
  private:
    bool stato;

  public:
    BottoneMixer(int x, int y, int larghezza, int altezza);
    void toggle_on_off();
    void set_on_off(bool);
    const bool get_on_off();
    int handle(int evento) override;
};

#endif // BOTTONE_MIXER_H_INCLUDED
