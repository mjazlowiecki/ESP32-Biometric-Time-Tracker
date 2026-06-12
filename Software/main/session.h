#ifndef SESSION_H
#define SESSION_H

#include "common.h"

// Zeruje tablicę sesji użytkowników (wywołane raz przy starcie)
void init_users(void);

// Zwraca wskaźnik na sesję o danym ID, lub NULL jeśli nie istnieje
UserSession* get_user(int id);

// Tworzy nową sesję dla danego ID (lub zwraca istniejącą, jeśli już jest)
UserSession* create_user(int id);

// Usuwa sesję użytkownika (oznacza slot jako wolny)
void remove_user(int id);

// Liczy aktywnych użytkowników: *p = liczba pracujących, *u = liczba uczących się
void get_active_stats(int *p, int *u);

#endif // SESSION_H