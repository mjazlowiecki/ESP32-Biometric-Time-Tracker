#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

#include "common.h"
#include "rtc.h"
#include "fingerprint.h"
#include "display.h"
#include "session.h"
#include "config.h"

// --- ZMIENNE GLOBALNE (deklaracje z common.h) ---
u8g2_t u8g2;
bool sd_available = false;
volatile int encoder_pos = 0;
volatile bool button_flag = false;
int next_enroll_id = 1;
int pending_alert_id = -1;
int alert_beep_count = 0;
int menu_step = MENU_IDLE;
int current_user_id = -1;

UserSession users[MAX_USERS];
SystemConfig config;

// --- EFEKTY (LED / BUZZER) ---
void beep(int freq, int ms) {
    int delay_us = 1000000 / freq / 2;
    int cycles = (long)freq * ms / 1000;
    for (int i = 0; i < cycles; i++) {
        gpio_set_level(PIN_BUZZER, 1); esp_rom_delay_us(delay_us);
        gpio_set_level(PIN_BUZZER, 0); esp_rom_delay_us(delay_us);
    }
}

void led_set(int g, int r, int y) {
    gpio_set_level(PIN_LED_GREEN, g);
    gpio_set_level(PIN_LED_RED, r);
    gpio_set_level(PIN_LED_YELLOW, y);
}

void signal_ok(void) {
    led_set(1, 0, 0);
    beep(2000, 100);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    led_set(0, 0, 0);
}

void signal_error(void) {
    led_set(0, 1, 0);
    beep(200, 500);
    led_set(0, 0, 0);
}

void beep_click(void) {
    beep(2000, 30);
}

// --- BACKGROUND TIMERS ---
// Sprawdza limity czasowe (praca/nauka/przerwa/dzień) dla wszystkich
// aktywnych użytkowników i ustawia stan ALARMU jeśli ktoś przekroczy limit.
void check_background_timers(void) {
    // JEŚLI JEST ALARM LUB MENU JEST OTWARTE -> nie sprawdzać czasu (blokada pętli)
    if (pending_alert_id != -1 || menu_step != MENU_IDLE) return;

    time_t now = get_seconds();
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].user_id == -1) continue;

        // Żółta dioda jak ktoś ma przerwę
        if (users[i].status == STATUS_BREAK) gpio_set_level(PIN_LED_YELLOW, 1);
        else gpio_set_level(PIN_LED_YELLOW, 0);

        if (users[i].status == STATUS_WORKING || users[i].status == STATUS_STUDYING) {
            int current_session = (now - users[i].start_time) / 60;
            int total_today = users[i].total_worked_minutes + current_session;

            if (total_today >= config.day_limit) {
                users[i].status = STATUS_ALERT_END_DAY;
                pending_alert_id = users[i].user_id;
                return;
            }
            int limit = (users[i].status == STATUS_WORKING) ? config.work_limit : config.study_limit;
            if (current_session >= limit) {
                users[i].status = STATUS_ALERT_WORK;
                pending_alert_id = users[i].user_id;
                return;
            }
        }
        if (users[i].status == STATUS_BREAK) {
            int elapsed = (now - users[i].break_start_time) / 60;
            if (elapsed >= config.break_limit) {
                users[i].status = STATUS_ALERT_BREAK;
                pending_alert_id = users[i].user_id;
                return;
            }
        }
    }
}

// --- ISR ---
void IRAM_ATTR isr_btn(void) {
    static uint32_t l = 0;
    uint32_t n = esp_timer_get_time() / 1000;
    if (n - l > 300) { button_flag = true; l = n; }
}

void IRAM_ATTR isr_enc(void) {
    static int8_t l = 0;
    int8_t c = (gpio_get_level(ENC_SIA_PIN) << 1) | gpio_get_level(ENC_SIB_PIN);
    if ((l == 0 && c == 2) || (l == 3 && c == 1)) encoder_pos++;
    else if ((l == 0 && c == 1) || (l == 3 && c == 2)) encoder_pos--;
    l = c;
}

// --- OBSŁUGA POSZCZEGÓLNYCH STANÓW MENU ---
// Każdy stan wydzielony jako funkcja - czytelniejsze niż jeden wielki if/else w pętli głównej.

static void handle_alert(void) {
    static int flash = 0;
    flash = !flash;
    led_set(0, flash, 0);
    if (flash && alert_beep_count < 10) { beep(2000, 100); alert_beep_count++; }

    UserSession* u = get_user(pending_alert_id);
    char msg[32];

    if (u->status == STATUS_ALERT_WORK) {
        sprintf(msg, "ALARM ID: %d", pending_alert_id);
        draw_ui(msg, "Koniec Czasu!", "Przyloz palec!");
    } else if (u->status == STATUS_ALERT_BREAK) {
        sprintf(msg, "KONIEC PRZERWY %d", pending_alert_id);
        draw_ui(msg, "Wracaj do pracy!", "Przyloz palec!");
    } else if (u->status == STATUS_ALERT_END_DAY) {
        sprintf(msg, "KONIEC DNIA ID:%d", pending_alert_id);
        draw_ui(msg, "Limit Osiagniety", "Przyloz zeby wyjsc");
    }

    if (button_flag) {
        button_flag = false;
        int scanned_id = scan_blocking();

        if (scanned_id == pending_alert_id || scanned_id == ADMIN_ID) {
            beep_click();
            led_set(0, 0, 0);
            alert_beep_count = 0; // Reset beepera na przyszłość

            if (u->status == STATUS_ALERT_WORK) {
                u->total_worked_minutes += (get_seconds() - u->start_time) / 60;
                write_csv(u->user_id, "PRACA_LIMIT", config.work_limit, config.work_limit, "PRZERWA");
                u->status = STATUS_BREAK;
                u->break_start_time = get_seconds();
                u->breaks_taken_count++;
                draw_ui("STATUS", "Rozpoczeto", "Przerwe");
                pending_alert_id = -1; // odblokuj alarm od razu

            } else if (u->status == STATUS_ALERT_BREAK) {
                u->total_break_minutes += (get_seconds() - u->break_start_time) / 60;
                current_user_id = (scanned_id == ADMIN_ID) ? u->user_id : scanned_id;
                menu_step = MENU_EXTEND_BREAK; // Pytanie o przedłużenie
                pending_alert_id = -1; // menu przejmuje kontrolę

            } else if (u->status == STATUS_ALERT_END_DAY) {
                write_csv(u->user_id, "KONIEC_DNIA", u->total_worked_minutes, config.day_limit, "WYLOGOWANIE");
                u->status = STATUS_OFFLINE;
                remove_user(u->user_id);
                draw_ui("DO WIDZENIA", "Koniec pracy", "");
                pending_alert_id = -1;
                vTaskDelay(2000 / portTICK_PERIOD_MS);
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS);

        } else {
            draw_ui("BLAD", "Zly Palec!", "Sprobuj ponownie");
            signal_error();
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
}

static void handle_extend_break_menu(void) {
    UserSession* u = get_user(current_user_id);
    int enc = encoder_pos;
    if (enc < 0) { encoder_pos = 0; enc = 0; }
    if (enc > 1) { encoder_pos = 1; enc = 1; }

    draw_ui("PRZEDLUZYC?", enc == 0 ? "> TAK" : "  TAK", enc == 1 ? "> NIE" : "  NIE");

    if (button_flag) {
        button_flag = false;

        if (enc == 0 && u->extensions_used < config.max_extensions) {
            u->extensions_used++;
            u->break_start_time = get_seconds();
            u->status = STATUS_BREAK;
            write_csv(u->user_id, "PRZERWA_EXT", 0, config.break_limit, "PRZEDLUZONO");
            draw_ui("OK", "Przedluzono", "");
        } else {
            if (enc == 0) {
                draw_ui("BLAD", "Limit osiagniety", "");
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }
            u->status = (u->user_id == ADMIN_ID) ? STATUS_WORKING : STATUS_STUDYING;
            u->start_time = get_seconds();
            write_csv(u->user_id, "KONIEC_PRZERWY", 0, 0, "POWROT");
            draw_ui("OK", "Do Pracy!", "");
        }

        menu_step = MENU_IDLE;
        current_user_id = -1;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static void handle_idle(void) {
    int p_cnt, u_cnt;
    get_active_stats(&p_cnt, &u_cnt);

    char dash_buf[32];
    sprintf(dash_buf, "P:%d U:%d", p_cnt, u_cnt);
    draw_ui("SYSTEM RCP", "Gotowy", dash_buf);

    // Obrót enkodera o 5+ w stanie spoczynku = wejście w rejestrację
    if (encoder_pos > 5) { encoder_pos = 0; enroll_blocking(); }

    if (button_flag) {
        button_flag = false;
        draw_ui("LOGOWANIE", "Przyloz palec...", "...");
        int id = scan_blocking();

        if (id > 0) {
            current_user_id = id;
            signal_ok();
            UserSession* u = create_user(id);

            if (id == ADMIN_ID) menu_step = MENU_ADMIN;
            else if (u->status == STATUS_OFFLINE) menu_step = MENU_CHOOSE_MODE;
            else menu_step = MENU_DASHBOARD;

            encoder_pos = 0;
        } else if (id == -2) {
            draw_ui("BLAD", "Nieznany!", "");
            signal_error();
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        } else {
            beep_click();
        }
    }
}

static void handle_choose_mode_menu(UserSession* u) {
    int enc = encoder_pos;
    if (enc < 0) { encoder_pos = 0; enc = 0; }
    if (enc > 1) { encoder_pos = 1; enc = 1; }

    draw_ui("Witaj!", enc == 0 ? "> PRACA" : "  PRACA", enc == 1 ? "> NAUKA" : "  NAUKA");

    if (button_flag) {
        button_flag = false;
        u->start_time = get_seconds();
        u->status = (enc == 0) ? STATUS_WORKING : STATUS_STUDYING;
        write_csv(u->user_id, (enc == 0) ? "START_PRACA" : "START_NAUKA", 0, 0, "START");
        signal_ok();
        current_user_id = -1;
        menu_step = MENU_IDLE;
    }
}

static void handle_dashboard(UserSession* u) {
    time_t now = get_seconds();
    int remaining = 0;

    if (u->status == STATUS_WORKING) remaining = config.work_limit - ((now - u->start_time) / 60);
    else if (u->status == STATUS_STUDYING) remaining = config.study_limit - ((now - u->start_time) / 60);
    else if (u->status == STATUS_BREAK) remaining = config.break_limit - ((now - u->break_start_time) / 60);

    draw_dashboard(u, remaining);

    if (button_flag) {
        button_flag = false;
        // Krótkie kliknięcie = powrót do IDLE
        current_user_id = -1;
        menu_step = MENU_IDLE;
    }

    // Przytrzymanie enkodera w lewo = ręczne wylogowanie
    if (encoder_pos < -2) {
        write_csv(u->user_id, "WYLOGOWANIE_RECZNE", 0, 0, "KONIEC");
        u->status = STATUS_OFFLINE;
        remove_user(current_user_id);
        draw_ui("KONIEC", "Wylogowano", "");
        signal_ok();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        current_user_id = -1;
        menu_step = MENU_IDLE;
        encoder_pos = 0;
    }
}

static void handle_admin_menu(void) {
    int enc = encoder_pos;
    if (enc < 0) { encoder_pos = 0; enc = 0; }
    if (enc > 1) { encoder_pos = 1; enc = 1; }

    draw_ui("ADMIN", enc == 0 ? "> Dodaj Usera" : "  Dodaj Usera", enc == 1 ? "> Wyloguj" : "  Wyloguj");

    if (button_flag) {
        button_flag = false;
        if (enc == 0) {
            enroll_blocking();
        } else {
            current_user_id = -1;
            menu_step = MENU_IDLE;
        }
    }
}

// --- INICJALIZACJA SPRZĘTU ---

static void init_gpio(void) {
    gpio_install_isr_service(0);

    gpio_reset_pin(PIN_LED_GREEN);  gpio_set_direction(PIN_LED_GREEN, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_LED_RED);    gpio_set_direction(PIN_LED_RED, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_LED_YELLOW); gpio_set_direction(PIN_LED_YELLOW, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_BUZZER);     gpio_set_direction(PIN_BUZZER, GPIO_MODE_OUTPUT);

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << ENC_SW_PIN) | (1ULL << ENC_SIA_PIN) | (1ULL << ENC_SIB_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .intr_type = 3
    };
    gpio_config(&io);
    gpio_isr_handler_add(ENC_SW_PIN, isr_btn, NULL);
    gpio_isr_handler_add(ENC_SIA_PIN, isr_enc, NULL);
    gpio_isr_handler_add(ENC_SIB_PIN, isr_enc, NULL);
}

static void init_display(void) {
    u8g2_esp32_hal_t uh = U8G2_ESP32_HAL_DEFAULT;
    uh.bus.i2c.sda = I2C_SDA;
    uh.bus.i2c.scl = I2C_SCL;
    u8g2_esp32_hal_init(uh);

    u8g2_Setup_sh1106_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8g2_esp32_i2c_byte_cb, u8g2_esp32_gpio_and_delay_cb);
    u8x8_SetI2CAddress(&u8g2.u8x8, OLED_ADDR << 1);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
}

static void init_fingerprint_uart(void) {
    uart_config_t uc = {57600, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1, UART_HW_FLOWCTRL_DISABLE, UART_SCLK_DEFAULT};
    uart_driver_install(UART_PORT, 1024, 0, 0, NULL, 0);
    uart_param_config(UART_PORT, &uc);
    uart_set_pin(UART_PORT, FP_TX_PIN, FP_RX_PIN, -1, -1);
}

static void init_sd_card(void) {
    esp_vfs_fat_sdmmc_mount_config_t mc = {.format_if_mount_failed = true, .max_files = 5, .allocation_unit_size = 16 * 1024};
    sdmmc_card_t *card;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 400;

    spi_bus_config_t bc = {.mosi_io_num = PIN_MOSI, .miso_io_num = PIN_MISO, .sclk_io_num = PIN_CLK, .quadwp_io_num = -1, .quadhd_io_num = -1};
    spi_bus_initialize(host.slot, &bc, SDSPI_DEFAULT_DMA);

    sdspi_device_config_t sc = SDSPI_DEVICE_CONFIG_DEFAULT();
    sc.gpio_cs = PIN_CS;
    sc.host_id = host.slot;

    if (esp_vfs_fat_sdspi_mount("/sdcard", &host, &sc, &mc, &card) == ESP_OK) {
        sd_available = true;
        load_config();
        load_next_id();
        beep_click();
    }
}

// --- MAIN ---
void app_main(void) {
    init_gpio();
    init_display();
    init_fingerprint_uart();
    init_sd_card();

    init_users();
    signal_ok();

    while (1) {
        check_background_timers();

        // --- 1. ALARM (PRIORYTET) ---
        if (pending_alert_id != -1) {
            handle_alert();
            continue;
        }

        // --- 2. EXTEND BREAK MENU ---
        if (menu_step == MENU_EXTEND_BREAK) {
            handle_extend_break_menu();
            continue;
        }

        // --- 3. IDLE ---
        if (current_user_id == -1) {
            handle_idle();
        }
        // --- 4. USER MENU ---
        else {
            UserSession* u = get_user(current_user_id);

            switch (menu_step) {
                case MENU_CHOOSE_MODE:  handle_choose_mode_menu(u); break;
                case MENU_DASHBOARD:    handle_dashboard(u);        break;
                case MENU_ADMIN:        handle_admin_menu();        break;
                default: break;
            }
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}