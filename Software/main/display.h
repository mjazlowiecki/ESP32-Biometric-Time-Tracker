#ifndef DISPLAY_H
#define DISPLAY_H

#include "common.h"

// Rysuje ekran z tytułem, główną treścią i linią dolną
// (np. status systemu, menu wyboru, komunikaty alarmowe)
void draw_ui(const char* title, const char* content, const char* bottom);

// Rysuje dashboard z aktualnym statusem zalogowanego użytkownika
// (czas do przerwy, liczba przerw, sumaryczny czas przerw)
void draw_dashboard(UserSession* u, int remaining_min);

#endif // DISPLAY_H