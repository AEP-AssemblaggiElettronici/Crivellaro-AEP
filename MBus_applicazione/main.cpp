#include <windows.h>
#include <stdio.h>

int main() {
    HANDLE hComm;
    hComm = CreateFileA("COM3", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

    if (hComm == INVALID_HANDLE_VALUE)
    {
        printf("Errore nell'apertura della porta seriale.\n");
        return 1;
    }

    // Configurazione seriale
    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hComm, &dcbSerialParams))
    {
        printf("Errore nella lettura delle impostazioni seriali.\n");
        CloseHandle(hComm);
        return 1;
    }

    dcbSerialParams.BaudRate = CBR_2400;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = EVENPARITY;

    if (!SetCommState(hComm, &dcbSerialParams))
    {
        printf("Errore nella configurazione della porta seriale.\n");
        CloseHandle(hComm);
        return 1;
    }

    // Messaggio di richiesta M-Bus
    unsigned char mbus_request[] = { 0x00, 0xE5 };// { 0x00/*bit_di_start*/, 0x68, 0x03, 0x03, 0x68, 0x73, 0xFF, 0x50, 0xC2, 0x16};
    //unsigned char pacchetto_due[] = { 0x68, 0x11, 0x11, 0x68, 0x53, 0xFD, 0x52, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x9E, 0x16 };
    DWORD bytesWritten;
     
    printf("Invio dati: ");
    for (size_t i = 0; i < sizeof(mbus_request); i++)
        printf("%02X ", mbus_request[i]);
    printf("\n");
    /*printf("Invio pacchetto supplementare: ");
    for (size_t i = 0; i < sizeof(pacchetto_due); i++)
        printf("%02X ", pacchetto_due[i]);
    printf("\n");
    */
    if (!WriteFile(hComm, mbus_request, sizeof(mbus_request), &bytesWritten, NULL))
    {
        printf("Errore nella scrittura sulla porta seriale.\n");
        CloseHandle(hComm);
        return 1;
    }
    Sleep(150);
    /*
    for (int i = 0; i < 3; i++)
    {
        if (!WriteFile(hComm, pacchetto_due, sizeof(pacchetto_due), &bytesWritten, NULL))
        {
            printf("Errore nella scrittura sulla porta seriale. Dati supplementari\n");
            CloseHandle(hComm);
            return 1;
        }
        Sleep(150);
    }*/

    printf("Richiesta inviata, in attesa di risposta...\n");

    // Lettura risposta
    unsigned char buffer[256] = { 0 };
    DWORD bytesRead = 0;
    Sleep(1500);

    while (!ReadFile(hComm, buffer, sizeof(buffer) - 1, &bytesRead, NULL))
        printf("Attesa di una risposta...\n");

    printf("Risposta ricevuta (%d byte): ", bytesRead);
    for (DWORD i = 0; i < bytesRead; i++)
        printf("%02X ", buffer[i]);
    printf("\n");

    CloseHandle(hComm);
return 0;
}
