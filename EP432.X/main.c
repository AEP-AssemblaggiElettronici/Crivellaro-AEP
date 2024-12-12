#include <xc.h>

#define _XTAL_FREQ 4000000

#pragma config OSC = IntRC      // Oscillator Selection bits (internal RC oscillator)
#pragma config WDT = OFF        // Watchdog Timer Enable bit (WDT disabled)
#pragma config CP = OFF         // Code Protection bit (Code protection off)
#pragma config MCLRE = OFF       // GP3/MCLR Pin Function Select bit (GP3/MCLR pin function is MCLR)

unsigned char ctrl = 0b11; // variabile di controllo, da sinistra: stato dell'input, direzione sfumatura
unsigned char rgbControl = 0;
unsigned char fade = 0;
unsigned char rgbBox[] = {0b010000, 0b100000, 0b000100}; // colore dei led accesi: R, G, B

void main(void)
{  
    TRISGPIO = 0b00000001; // definizione ingressi e uscite dell'MCU: 1 = input, 0 = output
    OPTION = 0b00000000;  // utilizzo il timer interno, bit5 a 0

    while(1)
    {           
        GPIO = rgbBox[rgbControl];
        
        if (GPIO & (1 << 0)) // touch input
        {
            if (!(ctrl & (1 << 1))) // debounce
            {
                rgbControl++;
                if (rgbControl > 2)
                    rgbControl = 0;
            }

            if (ctrl & (1 << 0)) // controllo direzione PWM
                fade++;
            else
                fade--;

            if (fade >= 128) // controllo valori PWM
                ctrl &= ~(1 << 0);  // direzione fade -
            if (fade <= 0)
                ctrl |= (1 << 0); // direzione fade +
            
            ctrl |= (1 << 1); // stato tocco a 1
        }
        else // debounce
            ctrl &= ~(1 << 1); // stato tocco a 0
        
        for (unsigned char i = 0; i < 128; i++) // PWM!
        {
            if (i < fade) 
                GPIO = rgbBox[rgbControl];
            else 
            GPIO = 0;
            __delay_us(100);
        }
    }
    return;
}