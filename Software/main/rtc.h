#ifndef RTC_H
#define RTC_H

#include <stdint.h>
#include <time.h>

// Konwersje BCD <-> dziesiętne (format używany przez DS1307)
uint8_t bcd2dec(uint8_t val);
uint8_t dec2bcd(uint8_t val);

// Odczyt czasu jako string "HH:MM" (do wyświetlenia na OLED)
void get_time_str(char *buffer);

// Odczyt pełnej daty jako string "YYYY-MM-DD;HH:MM" (do logów CSV)
void get_full_date(char *buffer);

// Odczyt aktualnego czasu w sekundach (do liczenia czasu trwania sesji)
time_t get_seconds(void);

// Ustawienie czasu RTC (wywoływane przy konfiguracji z karty SD)
void set_rtc_time(int year, int month, int day, int hour, int minute, int second);

#endif // RTC_H