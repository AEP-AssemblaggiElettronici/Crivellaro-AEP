#include <Arduino.h>

void battery_init()
{
    // Abilita il clock per l'ADC
    PM->APBCMASK.reg |= PM_APBCMASK_ADC;

    // Abilita il clock generatore GCLK0 per l'ADC
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_ADC |    // ADC ID
                        GCLK_CLKCTRL_GEN_GCLK0 | // GCLK0 = 48MHz default
                        GCLK_CLKCTRL_CLKEN;
    while (GCLK->STATUS.bit.SYNCBUSY)
        ;

    // Configura PA03 (PIN 3 del PORTA) come ingresso analogico
    PORT->Group[0].PINCFG[3].bit.PMUXEN = 1;                   // Abilita il mux
    PORT->Group[0].PMUX[3 >> 1].bit.PMUXO = PORT_PMUX_PMUXO_B; // Function B = analog

    // Configura ADC
    ADC->CTRLA.bit.ENABLE = 0; // Disattiva prima di configurare
    while (ADC->STATUS.bit.SYNCBUSY)
        ;

    ADC->REFCTRL.bit.REFSEL = ADC_REFCTRL_REFSEL_INTVCC1_Val;           // VREF = VCC/1.48
    ADC->AVGCTRL.reg = ADC_AVGCTRL_SAMPLENUM_1 | ADC_AVGCTRL_ADJRES(0); // no media
    ADC->SAMPCTRL.reg = 5;                                              // tempo di campionamento

    ADC->CTRLB.reg = ADC_CTRLB_PRESCALER_DIV512 | ADC_CTRLB_RESSEL_12BIT;
    while (ADC->STATUS.bit.SYNCBUSY)
        ;

    ADC->INPUTCTRL.bit.MUXPOS = ADC_INPUTCTRL_MUXPOS_PIN1_Val; // PA03 = AIN1
    while (ADC->STATUS.bit.SYNCBUSY)
        ;

    ADC->CTRLA.bit.ENABLE = 1; // Riattiva ADC
    while (ADC->STATUS.bit.SYNCBUSY)
        ;
}

/* float */uint16_t battery_read()
{
    ADC->SWTRIG.bit.START = 1;
    while (!ADC->INTFLAG.bit.RESRDY)
        ;

    uint16_t value = ADC->RESULT.reg;

    // Calcolo tensione approssimativa, considerando che il riferimento è VCC / 1.48
    // float voltage = (value / 4095.0) * (3.3 / 1.48); // 3.3V è VCC nominale

    return value;
}
