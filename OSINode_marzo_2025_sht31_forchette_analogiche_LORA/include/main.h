void (*reboot)() = 0; // !!!RESET FUNCTION!!!
String getID();
String getPAC();
void sendMessageSF(uint8_t[], int);
long readVcc();
void buzzer(int);