#include <xc.h>

#define _XTAL_FREQ 4000000

#pragma config FOSC = INTOSC  // Oscillator Selection bits (internal RC oscillator)
#pragma config WDTE = 01      // Watchdog Timer Enable bit (timer watchdog abilitato e controllato dal codice)
#pragma config CP = OFF       // Code Protection bit (Code protection off)
#pragma config LVP = 0        // Low Voltage Programming (setting it OFF, so I can set the MCLR as an input)
#pragma config MCLRE = 0      // GP3/MCLR Pin Function Select bit (GP3/MCLR pin function is MCLR)

void five_in_three(void);
void timer_init(void);

unsigned char bloc = 0; // Blocco input una volta lanciato l'impulso
int msec = 0;           // Unità temporali per timer e interrupt del timer
unsigned char sec = 0;
unsigned char min = 0;

void main(void)
{
    ANSELA = 0b000;                                    // Gli input sono tutti digitali
    TRISA = 0b1001;                                    // Impostiamo i  pin di input e outpu
    WPUA = 0b1001;                                     // Abilita pull-up interni su RA0 e RA3
    WDTCON = 0b011110;                                 // Intervallo watchdog timer di 32 secondi (il primo bit setta lo stato del watchdog)
    PORTA &= ~(1 << 1);                                // Buzzer basso all'avvio
    __delay_ms(500);
    PORTA &= ~(1 << 1);
    timer_init();
    
    while (1)
    {        
        if ((PORTA & 1) && !(PORTA & (1 << 3))) // mam
        {
            if (!bloc)
            {
                bloc = 1;
                five_in_three();
            }
        }
        else if (!(PORTA & 1) && (PORTA & (1 << 3))) // dreno
        {
            if (!bloc)
            {
                bloc = 1;
                WDTCON |= 1; // abilita watchdog
                for (unsigned char i = 0; i < 30; i++)
                {
                    five_in_three();
                    __delay_ms(28000);
                }
                PORTA |= (1 << 2); // buzzer!
                __delay_ms(3000);
                PORTA &= ~(1 << 2);
                WDTCON &= ~1; // disabilita watchdog
            }
        }
        bloc = 0;
    }
}

void five_in_three(void) // 5 impulsi in 3 secondi
{
    for (unsigned char j = 0; j < 5; j++)
    {
        PORTA |= (1 << 1);
        __delay_ms(600);
        PORTA &= ~(1 << 1);
        __delay_ms(600);
    }
}

void timer_init(void)
{
    OPTION_REG &= ~(1 << 5);                           // T0CS register set to 0 (internal instruction cycle clock)
    OPTION_REG &= ~(1 << 3);                           // Prescaler attivo
    OPTION_REG = (OPTION_REG & ~0b00000111) | (0b111); // Impostiamo il prescaler
    TMR0 = 240;                                        // Valore iniziale del timer (il timer andrà in overflow ogni millisecondo circa)
    INTCON |= (1 << 7);                                // Abilita interrupt globali
    INTCON |= (1 << 5);                                // Abilita interrupt su timer 0
}

void __interrupt() TIMER0_ISR(void)
{
    if (INTCON & (1 << 5))
    {
        TMR0 = 240;          // resetta il timer a 240
        INTCON &= ~(1 << 2); // resetta il flag di interrupt
        
        msec++;
        
        if (msec >= 1000)
        {
            msec = 0;
            sec++;
        }
        if (sec == 59)
        {
            sec = 0;
            min++;
        }
    }
}