/* EP417 colonnina distribuzione acqua - codice con intervallo erogazione in ciclo for e watchdog - 2025 Fabio Crivellaro */
#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include "i2cUtils.h"

#define LED1 11
#define LED2 12
#define KEY_RESET 7
#define KEY_CHANGE 13
#define EV1 2   // elettrovalvole
#define EV2 3   // elettrovalvole
#define EV3 4   // elettrovalvole
#define EV4 5   // elettrovalvole
#define ALARM 6 // allarme allagamento colonnina
#define BUZZER 8
#define DEVICE 0x1C          // ID dispositivo touch
#define TEMPO_BICCHIERE 3000 // valori di erogazione di default
#define TEMPO_BOTTIGLIA 8000 // valori di erogazione di default
#define EEPROM_BICCHIERE 0
#define EEPROM_BOTTIGLIA 1
#define LED_ON 0b11
#define LED_OFF 0b01
#define GUARD_KEY 0x14
#define DEBUG 1 // DEBUG MODE

void azionaValvole(int, int, int);
void buzzer(int);
void tempo_erogazione(int);
void beep(int);
void all_leds(bool);

const byte elettroValvole[] = {EV1, EV2, EV3};
const byte ledErogazione[] = {39, 36, 35};
byte registroInput[6];         // array registri input pulsanti
byte tempRegistroInput = 0;    // variabile per debounce input pulsanti
byte LED[5];                   // array stato led
byte statoPulsanti[5];         // array stato pulsanti
unsigned long tempoErogazione; // nomen omen, il valore iniziale è quello del tempo di erogazione del bicchiere
unsigned int bicchiere;        // tempo di erogazione personalizzato
unsigned int bottiglia;        // tempo di erogazione personalizzato
unsigned long inizioPressione; // variabili di controllo tempo pressione touch per calcolare il tempo di erogazione
unsigned long durataPressione = 0;
unsigned long inizioPressioneOption;
bool bicchiereBottiglia = 0; // stato pulsanti selezione bicchiere o bottiglia, il valore iniziale è per il bicchiere
bool optionMode = 0;         // modalità opzioni
bool incrementiamo = 0;      // variabile di controllo durante l'incremento del tempo di erogazione
bool blocco = 0;             // variabile che blocca nei momenti critici
bool incrementiamoOption = 0;
bool alarm = 0;

void setup()
{
    // EEPROM first boot
    if (EEPROM.read(64) != 64)
    {
        EEPROM.write(64, 64);
        EEPROM.write(EEPROM_BICCHIERE, 3);
        EEPROM.write(EEPROM_BOTTIGLIA, 8);
    }

    // richiamiamo i valori di erogazione salvati in precedenza
    bicchiere = EEPROM.read(EEPROM_BICCHIERE) * 1000;
    bottiglia = EEPROM.read(EEPROM_BOTTIGLIA) * 1000;
    tempoErogazione = bicchiere;

    Serial.begin(115200); // Seriale PC
#if DEBUG
    Serial.println("Tempo erogazione bicchiere:");
    Serial.println(bicchiere);
    Serial.println("Tempo erogazione bottiglia");
    Serial.println(bottiglia);
#endif
    Wire.begin(); // join i2c bus (address optional for master)
    delay(1);

    pinMode(KEY_RESET, OUTPUT);
    pinMode(KEY_CHANGE, INPUT);
    pinMode(LED1, OUTPUT);
    pinMode(LED2, OUTPUT);
    pinMode(EV1, OUTPUT);
    pinMode(EV2, OUTPUT);
    pinMode(EV3, OUTPUT);
    pinMode(EV4, OUTPUT);
    pinMode(ALARM, INPUT);
    digitalWrite(LED1, 1);
    digitalWrite(LED2, 1);
    digitalWrite(KEY_RESET, 1);
    delay(100);

    ////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////// SETUP DISPOSITIVO TOUCH
    //////////////////////////////////////////////////////////// diverse scritture di bytes sui registri via I²C
    //////////////////////////////////////////////////////////// consultare il datasheet dell'ATMEL QT2120 per maggiori info
    ////////////////////////////////////////////////////////////
    i2c_write(DEVICE, 6, GUARD_KEY);
    delay(5);
    // set Key config
    for (byte i = 28; i <= 34; i++)
    {
        i2c_write(DEVICE, i, 0x04);
        delay(2);
    }
    i2c_write(DEVICE, 28, 0x04);
    delay(2);
    i2c_write(DEVICE, 30, 0x04);
    delay(2);
    i2c_write(DEVICE, 31, 0x04);
    delay(2);
    i2c_write(DEVICE, 32, 0x04);
    delay(2);
    i2c_write(DEVICE, 33, 0x04);
    delay(2);
    i2c_write(DEVICE, 34, 0x04);
    delay(2);
    // Set Key 1 as Guard Key
    i2c_write(DEVICE, 29, GUARD_KEY);
    delay(2);
    // Set Pulse/Scale config
    for (byte i = 40; i <= 46; i++)
    {
        i2c_write(DEVICE, i, 0x21);
        delay(1);
    }
    i2c_write(DEVICE, 8, 1);   // Low Power Mode
    i2c_write(DEVICE, 9, 5);   // statoPulsantiD
    i2c_write(DEVICE, 10, 2);  // ATD
    i2c_write(DEVICE, 11, 4);  // Detection Integrator default 4 minimo 1 massimo 32 // Influisce molto su sensibilità, più sale e meno è sensibile
    i2c_write(DEVICE, 12, 1);  // Touch recall delay
    i2c_write(DEVICE, 13, 25); // DHT Drift Hold time default 25
    i2c_write(DEVICE, 14, 0);  // Slider
    i2c_write(DEVICE, 15, 5);  // Charge time default=0
    i2c_write(DEVICE, 16, 3);  // Detect Threshold Pulsante Accensione default= 10
    i2c_write(DEVICE, 17, 12); // Detect Threshold Guard Key default= 10
    i2c_write(DEVICE, 18, 3);  // Detect Threshold Pulsante Colore Luce default= 10
    i2c_write(DEVICE, 19, 3);  // Detect Threshold Pulsante Colore Luce default= 10
    i2c_write(DEVICE, 20, 3);  // Detect Threshold Pulsante Colore Luce default= 10
    i2c_write(DEVICE, 21, 3);  // Detect Threshold Pulsante Colore Luce default= 10
    i2c_write(DEVICE, 22, 3);  // Detect Threshold Pulsante Colore Luce default= 10

    all_leds(1); // TEST: accende e spegne tutti i leds
    delay(250);
    all_leds(0);
    delay(250);
    ////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////

    // Accende di default il led della modalità bicchiere:
    i2c_write(DEVICE, 37, LED_ON);

    wdt_enable(WDTO_250MS); // watchdog abilitato, timeout di 250 millisecondi
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////    LOOP
////////////////////////////////////////////////////////////
void loop()
{
    wdt_reset();
    if (!alarm)
    {
        if (!digitalRead(ALARM)) // check allarme allagamento
            alarm = 1;

        if (digitalRead(KEY_CHANGE) == 0) // se il pin di controllo riceve un qualsiasi input dal touch...
        {
            i2c_read(DEVICE, 0, 4, registroInput);
            tempRegistroInput = registroInput[3]; // Debounce
            delay(15);
            i2c_read(DEVICE, 0, 4, registroInput);

            if (registroInput[3] != 0 && registroInput[3] == tempRegistroInput) // gestione input da touch
            {
                statoPulsanti[0] = (registroInput[3] & 0b00000001);      // statoPulsanti1
                statoPulsanti[1] = (registroInput[3] & 0b00000100) >> 2; // statoPulsanti2
                statoPulsanti[2] = (registroInput[3] & 0b00001000) >> 3; // statoPulsanti3
                statoPulsanti[3] = (registroInput[3] & 0b00010000) >> 4; // statoPulsanti4
                statoPulsanti[4] = (registroInput[3] & 0b00100000) >> 5; // statoPulsanti5
                statoPulsanti[5] = (registroInput[3] & 0b01000000) >> 6; // statoPulsanti6

                if (statoPulsanti[0]) // statoPulsanti1, acqua liscia
                {
                    if (!optionMode)
                        azionaValvole(ledErogazione[0], elettroValvole[0], statoPulsanti[0]);
                    else
                        tempo_erogazione(0);
                }

                if (statoPulsanti[1]) // statoPulsanti2, acqua frizzante fresca
                {
                    if (!optionMode)
                        azionaValvole(ledErogazione[1], elettroValvole[1], statoPulsanti[1]);
                    else
                        tempo_erogazione(1);
                }

                if (statoPulsanti[2]) // statoPulsanti3, acqua liscia fresca
                {
                    if (!optionMode)
                        azionaValvole(ledErogazione[2], elettroValvole[2], statoPulsanti[2]);
                    else
                        tempo_erogazione(2);
                }

                if (statoPulsanti[3]) // statoPulsanti4, bottiglia
                {
                    if (!optionMode)
                    {
                        if (!bicchiereBottiglia)
                        {
                            bicchiereBottiglia = 1;
#if DEBUG
                            Serial.println("Tempo erogazione bottiglia:");
                            Serial.println(bottiglia); // DEBUG
#endif
                            tempoErogazione = bottiglia;
                        }
                    }
                }

                if (statoPulsanti[4]) // statoPulsanti5, bicchiere
                {
                    if (!optionMode)
                    {
                        if (bicchiereBottiglia)
                        {
                            bicchiereBottiglia = 0;
#if DEBUG
                            Serial.println("Tempo erogazione bicchiere:");
                            Serial.println(bicchiere); // DEBUG
#endif
                            tempoErogazione = bicchiere;
                        }
                    }
                }

                if (statoPulsanti[5] && !blocco) // statoPulsanti6 premuto, option mode
                {
                    if (!incrementiamoOption)
                    {
                        inizioPressioneOption = millis();
                        incrementiamoOption = 1;
                    }
                    else if (millis() - inizioPressioneOption > 2000) // Pressione superiore a 2 secondi
                    {
                        optionMode = !optionMode;
                        buzzer(optionMode ? 4 : 2);
                        blocco = 1; // Blocca l'input fino al rilascio
#if DEBUG
                        Serial.println(optionMode ? "Entrata option mode" : "Uscita option mode");
#endif
                    }
                }
                else if (!statoPulsanti[5] && incrementiamoOption) // statoPulsanti6 rilasciato
                {
                    incrementiamoOption = 0;
                    blocco = 0;
                }
            }
        }

        if (!bicchiereBottiglia) // accende/spegne i led a seconda della selezione modalità bicchiere o bottiglia
        {
            if (!optionMode)
                i2c_write(DEVICE, 37, LED_ON);
            else // lampeggio option mode
                i2c_write(DEVICE, 37, millis() % 1000 > 500 ? LED_ON : LED_OFF);
            i2c_write(DEVICE, 38, LED_OFF);
        }
        else
        {
            if (!optionMode)
                i2c_write(DEVICE, 38, LED_ON);
            else // lampeggio option mode
                i2c_write(DEVICE, 38, millis() % 1000 > 500 ? LED_ON : LED_OFF);
            i2c_write(DEVICE, 37, LED_OFF);
        }
    }
    else // ALLARME ALLAGAMENTO COLONNINA ATTIVO!
    {
        if (millis() % 500 < 250)
        {
            all_leds(1);
            beep(50);
        }
        else
            all_leds(0);

        if (digitalRead(ALARM))
        {
            all_leds(0);
            alarm = 0;
        }
    }
    wdt_reset();
}
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void azionaValvole(int led, int valvola, int statoPulsante) // nomen omen, aziona valvole per il tempo prestabilito
{
    i2c_write(DEVICE, led, LED_ON); // accende il led corrispondente (la prima cifra è sempre 1)
    digitalWrite(valvola, 1);
    for (unsigned int i = 0; i < tempoErogazione; i++)
    {
        wdt_reset();
        delay(1);
    }
    digitalWrite(valvola, 0);
    i2c_write(DEVICE, led, LED_OFF); // spegne il led corrispondente
    statoPulsante = 0;
    wdt_reset();
}

void buzzer(int ripetizioni) // buzzer con lampeggio alternato
{
    byte led[2] = {39, 35};
    bool ledAccesoSpento = 0;
    for (ripetizioni; ripetizioni--;)
    {
        i2c_write(DEVICE, led[ledAccesoSpento], 0b11);
        i2c_write(DEVICE, led[!ledAccesoSpento], 0b01);
        beep(100);
        wdt_reset();
        delay(200);
        ledAccesoSpento ^= 1;
        for (byte i = 2; i--;)
            i2c_write(DEVICE, led[i], 0b01);
    }
}

void beep(int time)
{
    for (time; time--;)
    {
        digitalWrite(BUZZER, 1);
        delayMicroseconds(100);
        digitalWrite(BUZZER, 0);
        delayMicroseconds(100);
    }
}

void tempo_erogazione(int n) // nomen omen, questo blocco si occupa dell'edit del tempo di erogazione in modalità option
{
    if (!incrementiamo)
    {
        blocco = 1;

        switch (n) // a seconda del pulsante scelto in precedenza, apre la valvola e accende il led corrispondente
        {
        case 0:
            i2c_write(DEVICE, ledErogazione[0], LED_ON);
            digitalWrite(EV1, 1);
            break;
        case 1:
            i2c_write(DEVICE, ledErogazione[1], LED_ON);
            digitalWrite(EV2, 1);
            break;
        case 2:
            i2c_write(DEVICE, ledErogazione[2], LED_ON);
            digitalWrite(EV3, 1);
            break;
        }

#if DEBUG
        Serial.println("Inizio incremento tempo erogazione"); // DEBUG
#endif
        inizioPressione = millis();
        incrementiamo = 1;
    }
    else
    {
        // spengo assieme tutte le elettrovalvole e i led perchè si può fermare l'erogazione da qualsiasi touch
        for (byte i = 3; i--;)
        {
            i2c_write(DEVICE, ledErogazione[i], LED_OFF);
            digitalWrite(elettroValvole[i], 0);
        }

#if DEBUG
        Serial.println("Fine incremento tempo erogazione"); // DEBUG
#endif
        durataPressione = millis() - inizioPressione;
        // arrotondiamo per difetto o eccesso se i millisecondi sono superiori o inferiori a 500
        if (durataPressione % 1000 < 500)
            durataPressione = (durataPressione / 1000) * 1000;
        else
            durataPressione = ((durataPressione / 1000) + 1) * 1000;

        if (durataPressione > 255000)
            durataPressione = 255000;

        if (bicchiereBottiglia)
        {
            bottiglia = durataPressione;
            EEPROM.update(EEPROM_BOTTIGLIA, durataPressione / 1000);
        }
        else
        {
            bicchiere = durataPressione;
            EEPROM.update(EEPROM_BICCHIERE, durataPressione / 1000);
        }

        tempoErogazione = durataPressione;
        blocco = 0;
        incrementiamo = 0;
        optionMode = 0;
        buzzer(2);
    }
}

void all_leds(bool stato) // setta on o off tutti i led sul touch
{
    for (byte i = 35; i <= 39; i++)
        i2c_write(DEVICE, i, stato ? LED_ON : LED_OFF);
}