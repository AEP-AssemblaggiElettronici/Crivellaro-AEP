#define DEBUG 1 // MODALITA DEBUG (1 attiva, 0 disattivata)

/*
    con il pin 3 per la porta D e C dobbiamo discriminare la presenza della forchetta analogica o la bilancia
    tipo, quando è a 0 c'è la forchetta collegata, quando è a 1 c'è la bilancia collegata
    se c'è per esempio una fochetta, non può esserci una bilancia
    potrebbe essere fatto con un pullup
*/
/* RIDEFINIAMO... */
#define SDA_PIN A4
#define SCL_PIN A5
#define PORT_A 12
#define PORT_B 10
#define PORT_C_J_1_5 A1
#define PORT_C_J_1_4 13
#define PORT_C_J_1_3 11 //
#define PORT_D_J_4_5 A2
#define PORT_D_J_4_4 4
#define PORT_D_J_4_3 A3 //
#define I2C_SWITCH 8

// velocità di trasmissione varie per seriale, sigfox e lora:
#define BAUD 9600
#define SERIAL_SIGFOX 9600
#define SERIAL_LORA 1200
#define rxPin 6
#define txPin 7

#define BUZZER 5 // pin del buzzer

#define SFOX_RST 2 // reset sigfox

// indirizzo sensori SHT3x (è solo uno perchè vengono swtichati attraverso il pin 8)
#define SHT3X 0x44

#define LUXMETRO1 0x23
#define LUXMETRO2 0x23

#define BOOST_EN 3     // alimentazione sulle porte D e C
#define BOOST_SHTDWN 9 // 12v per alimentazione sensoristica analogica (porte D e C)
#define IO_ENABLE A0   // alimentazione sulle porte A e B

#define SCALA_PESO 0.5629 // 2.8089887640449 //2.2123

#define TEMPO_LORA 180000   // 3 minuti (per CARNEMOLLA)
#define TEMPO_SIGFOX 900000 // 15 minuti

#define COSTANTE_C 250 // costanti di peso
#define COSTANTE_D 250