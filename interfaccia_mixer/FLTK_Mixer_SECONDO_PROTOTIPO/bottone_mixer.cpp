#include "bottone_mixer.h"

BottoneMixer::BottoneMixer(int x, int y, int larghezza, int altezza, const char* label)
: Fl_Box(x, y, larghezza, altezza)
{
    set_on_off(1);
    this->copy_label(label);
    this->labelsize(9);
    this->redraw();
}

void BottoneMixer::toggle_on_off()
{
    this->stato ^= 1;
    this->box(stato ? FL_DOWN_BOX : FL_UP_BOX);
    this->color(stato ? FL_GREEN : FL_RED);
    this->redraw();
}

void BottoneMixer::set_on_off(bool stato)
{
    this->stato = stato;
    this->box(stato ? FL_DOWN_BOX : FL_UP_BOX);
    this->color(stato ? FL_GREEN : FL_RED);
    this->redraw();
}

const bool BottoneMixer::get_on_off() { return this->stato; }

int BottoneMixer::handle(int event)
{
    if (event == FL_PUSH)
    {
        BottoneMixer::toggle_on_off();
        return 1;
    }
    return Fl_Box::handle(event);
}
