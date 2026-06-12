#ifndef FINGERPRINT_H
#define FINGERPRINT_H

#include <stdint.h>

// Wysłanie pakietu komendy do czytnika R307 (protokół binarny przez UART)
void send_cmd(uint8_t pid, uint8_t len, uint8_t *content);

// Odebranie odpowiedzi od czytnika. Zwraca kod statusu (0 = OK, 0xFF = timeout)
uint8_t get_resp(uint8_t *d);

// Blokujące skanowanie palca + przeszukanie bazy odcisków.
// Zwraca: ID użytkownika (>0) jeśli rozpoznany, -2 jeśli odcisk nieznany, -1 jeśli timeout
int scan_blocking(void);

// Blokujący proces rejestracji nowego odcisku (2x skan + zapis do bazy czytnika)
void enroll_blocking(void);

#endif // FINGERPRINT_H