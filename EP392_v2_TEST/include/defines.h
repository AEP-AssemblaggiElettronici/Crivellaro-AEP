#define IO_ENABLE 22    // per attivazione I2C
#define I2C_SELECT 9    // per selezionare porta A o B quando IO_ENABLE è alzato
#define BOOST_EN 17     // se è su, alimenta i 4 volt su porte C e D, per le bilance
#define BOOST_SHTDWN 18 // se è su assieme a BOOST_EN, alimenta i 12 volt sulle porte C e D, per le forchette analogiche
#define BATTERY 15      // pin per misurare la tensione della batteria
#define FORKETT_RX 0    // RX seriale forchette RS485, porta E
#define FORKETT_TX 1    // TX seriale forchette RS485
#define FORKETT_RX2 31  // come sopra ma per la porta F
#define FORKETT_TX2 30

#define BAUD 115200
#define RADIO_BAUD 1200
#define FORKETT_BAUD 4800
#define SIGFOX_BAUD 9600

#define SHT3X_ADDRESS 0x44 // indirizzo I2C del sensore temperatura umidità

#define PIN_CENTRALE_C 8
#define PIN_CENTRALE_D 24
#define PIN_SDA 20
#define PIN_SCL 21
#define PIN_RADIO_RX 11 // 12
#define PIN_RADIO_TX 10
#define PIN_FORCHETTA_C 14 // AN0 - SCK bilancia
#define PIN_FORCHETTA_D 15 // AN2 - SCK bilancia
#define PIN_SDA_C 4        // SDA bilancia
#define PIN_SDA_D 23       // SDA bilancia
#define PIN_TX_ENABLE 13   // pin per abilitare la trasmissione sulle fochette RS485
#define PIN_PLUVIO_A 6     // pin pluviometro (interruptati)
#define PIN_PLUVIO_B 5
#define BUZZER 19
#define PIN_TEST 25
#define PIN_SIGFOX_RESET 16

//#define forkett Serial1 // sercom 0 - da conflitto con la radio, l'i2c, porta E
#define forkett2 Serial5 // sercom 5
const byte umiditaTemperatura[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x03, 0x05, 0xCB};

#define SCALA_PESO 0.5629 // 2.8089887640449 //2.2123
#define NUM_PIN 32