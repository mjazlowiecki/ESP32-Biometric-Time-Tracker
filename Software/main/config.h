#ifndef CONFIG_H
#define CONFIG_H

// Wczytuje ustawienia (limity czasowe) z /sdcard/config.txt.
// Jeśli plik zawiera "set_time=1", ustawia RTC na podaną datę/godzinę
// i zapisuje plik ponownie z "set_time=0" (żeby zegar nie cofał się
// przy każdym restarcie, a tylko gdy użytkownik tego chce).
void load_config(void);

// Wczytuje ostatnio użyte ID rejestracji z /sdcard/last_id.txt
void load_next_id(void);

// Zapisuje aktualne next_enroll_id do /sdcard/last_id.txt
void save_next_id(void);

// Dopisuje rekord zdarzenia do /sdcard/data.csv (no-op jeśli karta SD niedostępna)
void write_csv(int id, const char *act, int dur, int lim, const char *stat);

#endif // CONFIG_H