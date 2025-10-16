#define SERIAL_BAUD 115200
#define RADIO_BAUD 9600  // per sigfox
#define LORA_BAUD 1200   // per lora

#define RXpin 11
#define TXpin 10

#define PIN_SIGFOX_RESET 21

#define PIN_SDA 8
#define PIN_SCL 9

#define PIN_FORK_C 4  // prima era 1, che dava errore, ok input ma non output, sotto
#define PIN_BIL_C 3   // ok, sopra
#define PIN_FORK_D 1  // prima era 6, che dava errore, sotto
#define PIN_BIL_D 36  // ok, sopra
#define PIN_FORK_C_PRESENZA 14
#define PIN_FORK_D_PRESENZA 46

#define RS485_TX_1 12
#define RS485_RX_1 13
#define RS485_TX_2 15
#define RS485_RX_2 16
#define RS485_DE 17
#define RS485_RE 18
#define RS485_IN_C 14
#define RS485_IN_D 46

#define I2C_SELECT 39
#define SHT3X_ADDRESS 0x44
#define IO_ENABLE 35

#define BOOST_SHTDWN 19  // alzare ANCHE questo per leggere le FORCHETTE
#define BOOST_EN 20      // alzare SOLO questo per leggere le BILANCE

#define FORKETT_BAUD 4800
#define SCALA_PESO 0.5629  // 2.8089887640449 //2.2123
#define BUZZER 37
#define INT1 7
#define INT2 2
#define PIN_BATTERY 5  // pin ADC, tensione batteria

const byte umiditaTemperatura[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x03, 0x05, 0xCB };
const byte unusedPins[] = { 3, 47, 48, 45, 42, 41, 40 };