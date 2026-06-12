#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "u8g2.h"

// --- KONFIGURACJA PINÓW ---
#define PIN_LED_GREEN   13
#define PIN_LED_RED     12
#define PIN_LED_YELLOW  4
#define PIN_BUZZER      14
#define ENC_SIA_PIN     27
#define ENC_SIB_PIN     26
#define ENC_SW_PIN      25
#define FP_RX_PIN       17
#define FP_TX_PIN       16
#define UART_PORT       UART_NUM_2
#define I2C_SDA         21
#define I2C_SCL         22
#define RTC_ADDR        0x68
#define OLED_ADDR       0x3C
#define PIN_MISO        19
#define PIN_MOSI        23
#define PIN_CLK         18
#define PIN_CS          5

#define ADMIN_ID        1
#define MAX_USERS       10

// --- STANY MENU ---
// Zamiast magicznych liczb (50, 100, 150) - czytelne nazwy dla głównej maszyny stanów
typedef enum {
    MENU_IDLE          = 0,
    MENU_CHOOSE_MODE   = 1,    // Wybór: Praca / Nauka
    MENU_ADMIN         = 50,   // Panel admina
    MENU_EXTEND_BREAK  = 100,  // Pytanie o przedłużenie przerwy
    MENU_DASHBOARD     = 150   // Widok statusu użytkownika
} MenuStep;

// --- STATUSY UŻYTKOWNIKA ---
typedef enum {
    STATUS_OFFLINE,
    STATUS_WORKING,
    STATUS_STUDYING,
    STATUS_BREAK,
    STATUS_ALERT_WORK,
    STATUS_ALERT_BREAK,
    STATUS_ALERT_END_DAY
} UserStatus;

// --- SESJA UŻYTKOWNIKA ---
typedef struct {
    int user_id;
    UserStatus status;
    time_t start_time;
    time_t break_start_time;
    int extensions_used;
    int total_worked_minutes;
    int breaks_taken_count;
    int total_break_minutes;
} UserSession;

// --- KONFIGURACJA SYSTEMU (z config.txt) ---
typedef struct {
    int work_limit;
    int study_limit;
    int break_limit;
    int max_extensions;
    int day_limit;
} SystemConfig;

// --- ZMIENNE GLOBALNE (zdefiniowane w main.c, deklarowane tu jako extern) ---
extern u8g2_t u8g2;
extern bool sd_available;
extern volatile int encoder_pos;
extern volatile bool button_flag;
extern int next_enroll_id;
extern int pending_alert_id;
extern int alert_beep_count;
extern int menu_step;
extern int current_user_id;
extern UserSession users[MAX_USERS];
extern SystemConfig config;

// --- EFEKTY (LED / BUZZER) ---
// Zostają w common, bo są używane przez wszystkie moduły (sygnalizacja błędów/sukcesu)
void beep(int freq, int ms);
void led_set(int g, int r, int y);
void signal_ok(void);
void signal_error(void);
void beep_click(void);

#endif // COMMON_H