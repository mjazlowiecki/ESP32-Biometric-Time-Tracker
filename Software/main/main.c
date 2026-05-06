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

// --- KONFIGURACJA ---
#define PIN_LED_GREEN 13
#define PIN_LED_RED   12
#define PIN_LED_YELLOW 4
#define PIN_BUZZER    14
#define ENC_SIA_PIN   27  
#define ENC_SIB_PIN   26  
#define ENC_SW_PIN    25  
#define FP_RX_PIN     17  
#define FP_TX_PIN     16  
#define UART_PORT     UART_NUM_2
#define I2C_SDA       21
#define I2C_SCL       22
#define RTC_ADDR      0x68
#define OLED_ADDR     0x3C
#define PIN_MISO      19
#define PIN_MOSI      23
#define PIN_CLK       18
#define PIN_CS        5
#define ADMIN_ID      1 
#define MAX_USERS     10 

// --- STRUKTURY ---
typedef enum { 
    STATUS_OFFLINE, STATUS_WORKING, STATUS_STUDYING, STATUS_BREAK,
    STATUS_ALERT_WORK, STATUS_ALERT_BREAK, STATUS_ALERT_END_DAY  
} UserStatus;

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

UserSession users[MAX_USERS];

struct {
    int work_limit; int study_limit; int break_limit; int max_extensions; int day_limit;       
} config;

// --- ZMIENNE GLOBALNE ---
u8g2_t u8g2;
bool sd_available = false;
volatile int encoder_pos = 0;
volatile bool button_flag = false; 
int next_enroll_id = 1;
int pending_alert_id = -1;
int alert_beep_count = 0;
int menu_step = 0;          // 
int current_user_id = -1;   // 

// --- ZARZĄDZANIE ---
void init_users() {
    for(int i=0; i<MAX_USERS; i++) {
        users[i].user_id = -1; users[i].total_worked_minutes=0;
        users[i].breaks_taken_count=0; users[i].total_break_minutes=0;
    }
}
UserSession* get_user(int id) {
    for(int i=0; i<MAX_USERS; i++) if(users[i].user_id == id) return &users[i];
    return NULL;
}
UserSession* create_user(int id) {
    UserSession* u = get_user(id); if(u) return u; 
    for(int i=0; i<MAX_USERS; i++) {
        if(users[i].user_id == -1) {
            users[i].user_id = id; users[i].status = STATUS_OFFLINE;
            users[i].extensions_used = 0; users[i].total_worked_minutes = 0;
            return &users[i];
        }
    }
    return NULL;
}
void remove_user(int id) { UserSession* u = get_user(id); if(u) u->user_id = -1; }
void get_active_stats(int *p, int *u) {
    *p = 0; *u = 0;
    for(int i=0; i<MAX_USERS; i++) {
        if(users[i].user_id != -1) {
            if(users[i].status == STATUS_WORKING || users[i].status == STATUS_ALERT_WORK) (*p)++;
            if(users[i].status == STATUS_STUDYING) (*u)++;
        }
    }
}

// --- EFEKTY ---
void beep(int freq, int ms) {
    int delay_us = 1000000 / freq / 2;
    int cycles = (long)freq * ms / 1000;
    for(int i=0; i<cycles; i++) {
        gpio_set_level(PIN_BUZZER, 1); esp_rom_delay_us(delay_us);
        gpio_set_level(PIN_BUZZER, 0); esp_rom_delay_us(delay_us);
    }
}
void led_set(int g, int r, int y) {
    gpio_set_level(PIN_LED_GREEN, g); gpio_set_level(PIN_LED_RED, r); gpio_set_level(PIN_LED_YELLOW, y);
}
void signal_ok() { led_set(1, 0, 0); beep(2000, 100); vTaskDelay(500/portTICK_PERIOD_MS); led_set(0, 0, 0); }
void signal_error() { led_set(0, 1, 0); beep(200, 500); led_set(0, 0, 0); }
void beep_click() { beep(2000, 30); }

// --- RTC ---
uint8_t bcd2dec(uint8_t val) { return ((val / 16 * 10) + (val % 16)); }
void get_time_str(char *buffer) {
    uint8_t reg = 0x00; uint8_t data[7];
    if (i2c_master_write_read_device(I2C_NUM_1, RTC_ADDR, &reg, 1, data, 7, 100) == ESP_OK) {
        sprintf(buffer, "%02d:%02d", bcd2dec(data[2] & 0x3F), bcd2dec(data[1]));
    } else sprintf(buffer, "--:--");
}
void get_full_date(char *buffer) {
    uint8_t reg = 0x00; uint8_t data[7];
    if (i2c_master_write_read_device(I2C_NUM_1, RTC_ADDR, &reg, 1, data, 7, 100) == ESP_OK) {
        sprintf(buffer, "20%02d-%02d-%02d;%02d:%02d", bcd2dec(data[6]), bcd2dec(data[5]), bcd2dec(data[4]), bcd2dec(data[2] & 0x3F), bcd2dec(data[1]));
    } else sprintf(buffer, "ERR");
}
time_t get_seconds() {
    uint8_t reg = 0x00; uint8_t data[7];
    if (i2c_master_write_read_device(I2C_NUM_1, RTC_ADDR, &reg, 1, data, 7, 100) == ESP_OK) {
        return (bcd2dec(data[2] & 0x3F) * 3600) + (bcd2dec(data[1]) * 60) + bcd2dec(data[0] & 0x7F);
    } return 0;
}

// --- DANE ---
void write_csv(int id, const char *act, int dur, int lim, const char *stat) {
    if (!sd_available) return;
    char date[32]; get_full_date(date);
    FILE *f = fopen("/sdcard/data.csv", "a");
    if (f) {
        if(stat==NULL) stat="INFO";
        fprintf(f, "%s;%d;%s;%d;%d;%s\n", date, id, act, dur, lim, stat);
        fclose(f);
    }
}
// Funkcja pomocnicza: Decimal do BCD
uint8_t dec2bcd(uint8_t val) { return ((val / 10 * 16) + (val % 10)); }
// Funkcja wysyłająca czas do RTC
void set_rtc_time(int year, int month, int day, int hour, int minute, int second) {
    uint8_t data[8];
    data[0] = 0x00; // Start od rejestru sekund
    data[1] = dec2bcd(second);
    data[2] = dec2bcd(minute);
    data[3] = dec2bcd(hour);
    data[4] = 0x01; // Dzień tygodnia 
    data[5] = dec2bcd(day);
    data[6] = dec2bcd(month);
    data[7] = dec2bcd(year - 2000); // RTC trzyma rok jako 2 cyfry

    i2c_master_write_to_device(I2C_NUM_1, RTC_ADDR, data, 8, 1000 / portTICK_PERIOD_MS);
    printf("RTC: Ustawiono czas na %04d-%02d-%02d %02d:%02d:%02d\n", year, month, day, hour, minute, second);
}
void load_config() {
    FILE *f = fopen("/sdcard/config.txt", "r");
    
    // Domyślne wartości (zabezpieczenie)
    config.work_limit=60; config.study_limit=45; config.break_limit=15; config.max_extensions=2; config.day_limit=480;
    
    int set_time_flag = 0;
    char datetime_buf[64] = "2000-01-01 00:00:00"; // Bufor na date

    if(f) {
        char line[128];
        while(fgets(line, sizeof(line), f)) {
            // usun znak nowej linii na końcu, żeby nie śmiecił
            line[strcspn(line, "\n")] = 0;
            
            char *val_ptr = strchr(line, '=');
            if(val_ptr) {
                val_ptr++; // wskaźnik za znak '='
                
                if(strstr(line, "work")) config.work_limit = atoi(val_ptr);
                else if(strstr(line, "study")) config.study_limit = atoi(val_ptr);
                else if(strstr(line, "break")) config.break_limit = atoi(val_ptr);
                else if(strstr(line, "max_ext")) config.max_extensions = atoi(val_ptr);
                else if(strstr(line, "day_limit")) config.day_limit = atoi(val_ptr);
                else if(strstr(line, "set_time")) set_time_flag = atoi(val_ptr);
                else if(strstr(line, "datetime")) strcpy(datetime_buf, val_ptr);
            }
        }
        fclose(f);
    }

    // --- LOGIKA USTAWIANIA CZASU ---
    if (set_time_flag == 1) {
        int Y, M, D, h, m, s;
        // string parse
        if (sscanf(datetime_buf, "%d-%d-%d %d:%d:%d", &Y, &M, &D, &h, &m, &s) == 6) {
            set_rtc_time(Y, M, D, h, m, s);
            beep(1000, 100); beep(1000, 100); // Podwójny bip = sukces czasu
        }
        
        // nadpisanie pliku zeby zmienic set_time NA 0
        // dlatego zegar nie cofnie sie przy kazdym restarcie, a tylko wtedy gdy uzytkownik tego chce (ustawiajac set_time=1)
        f = fopen("/sdcard/config.txt", "w");
        if (f) {
            fprintf(f, "work=%d\n", config.work_limit);
            fprintf(f, "study=%d\n", config.study_limit);
            fprintf(f, "break=%d\n", config.break_limit);
            fprintf(f, "max_ext=%d\n", config.max_extensions);
            fprintf(f, "day_limit=%d\n", config.day_limit);
            fprintf(f, "set_time=0\n"); // <--- zmien na 0
            fprintf(f, "datetime=%s\n", datetime_buf); // Zostawiamy starą datę dla info
            fclose(f);
            printf("CONFIG: Zaktualizowano plik (set_time=0)\n");
        }
    }
}
void load_next_id() {
    if (!sd_available) { next_enroll_id = 2; return; }
    FILE *f = fopen("/sdcard/last_id.txt", "r");
    if (f) { fscanf(f, "%d", &next_enroll_id); fclose(f); if (next_enroll_id < 2) next_enroll_id = 2; } 
    else next_enroll_id = 2;
}
void save_next_id() {
    if (!sd_available) return;
    FILE *f = fopen("/sdcard/last_id.txt", "w");
    if (f) { fprintf(f, "%d", next_enroll_id); fclose(f); }
}

// --- CZUJNIK ---
void send_cmd(uint8_t pid, uint8_t len, uint8_t *content) {
    uart_flush_input(UART_PORT);
    uint8_t *pkt=malloc(9+len);
    const uint8_t HEADER[2]={0xEF,0x01}; const uint8_t ADDR[4]={0xFF,0xFF,0xFF,0xFF};
    memcpy(pkt, HEADER, 2); memcpy(pkt+2, ADDR, 4); pkt[6]=pid; pkt[7]=len>>8; pkt[8]=len;
    memcpy(pkt+9, content, len-2); uint16_t sum=pid+len; for(int i=0; i<len-2; i++) sum+=content[i];
    pkt[9+len-2]=sum>>8; pkt[9+len-1]=sum;
    uart_write_bytes(UART_PORT, (const char*)pkt, 9+len); free(pkt);
}
uint8_t get_resp(uint8_t *d) {
    uint8_t byte; int timeout=400; int64_t start=esp_timer_get_time()/1000; bool hdr=false;
    while((esp_timer_get_time()/1000)-start < timeout) {
        if(uart_read_bytes(UART_PORT, &byte, 1, 10/portTICK_PERIOD_MS)>0) {
            if(byte==0xEF) { uint8_t n; if(uart_read_bytes(UART_PORT, &n, 1, 20/portTICK_PERIOD_MS)>0 && n==0x01) { hdr=true; break; } }
        }
    }
    if(!hdr) return 0xFF;
    uint8_t buf[20]; int len=uart_read_bytes(UART_PORT, buf, 10, 100/portTICK_PERIOD_MS);
    if(len>=7) { if(d){d[0]=0xEF; d[1]=0x01; memcpy(d+2,buf,len);} return buf[7]; }
    return 0xFF;
}

// --- GUI ---
void draw_ui(const char* t, const char* c, const char* b) {
    u8g2_ClearBuffer(&u8g2); char clk[10]; get_time_str(clk);
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tf); u8g2_DrawStr(&u8g2, 0, 10, t); u8g2_DrawStr(&u8g2, 95, 10, clk); u8g2_DrawHLine(&u8g2, 0, 12, 128);
    if(strlen(c)<12) u8g2_SetFont(&u8g2, u8g2_font_ncenB14_tr); else u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&u8g2, 0, 40, c);
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tf); u8g2_DrawStr(&u8g2, 0, 60, b);
    u8g2_SendBuffer(&u8g2);
}
void draw_dashboard(UserSession* u, int remaining_min) {
    u8g2_ClearBuffer(&u8g2); u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(&u8g2, 0, 10, "DASHBOARD"); u8g2_DrawHLine(&u8g2, 0, 12, 128);
    char buf[32];
    sprintf(buf, "Do przerwy: %d m", remaining_min); u8g2_DrawStr(&u8g2, 0, 25, buf);
    sprintf(buf, "Przerw: %d", u->breaks_taken_count); u8g2_DrawStr(&u8g2, 0, 35, buf);
    sprintf(buf, "Czas przerw: %d m", u->total_break_minutes); u8g2_DrawStr(&u8g2, 0, 45, buf);
    u8g2_DrawStr(&u8g2, 0, 60, "Kliknij -> Wyjdz");
    u8g2_SendBuffer(&u8g2);
}

// --- LOGIC ---
int scan_blocking() {
    uint8_t gen[]={0x01}; uint8_t chr[]={0x02,0x01}; uint8_t srch[]={0x04,0x01,0x00,0x00,0x03,0xE8}; uint8_t rx[30];
    for(int i=0; i<50; i++) {
        send_cmd(0x01,3,gen);
        if(get_resp(NULL)==0) {
            send_cmd(0x01,4,chr);
            if(get_resp(NULL)==0) {
                send_cmd(0x01,8,srch); get_resp(rx);
                if(rx[9]==0) return (rx[10]<<8)|rx[11];
                if(rx[9]==9) return -2;
            }
        }
        vTaskDelay(100/portTICK_PERIOD_MS);
    }
    return -1;
}

void enroll_blocking() {
    uint8_t gen[]={0x01}; uint8_t ch1[]={0x02,0x01}; uint8_t ch2[]={0x02,0x02}; uint8_t reg[]={0x05};
    char buf[20]; sprintf(buf, "ID: %d", next_enroll_id);
    draw_ui("ADMIN", buf, "Poloz palec...");
    while(1) { send_cmd(0x01,3,gen); if(get_resp(NULL)==0) break; vTaskDelay(50/portTICK_PERIOD_MS); }
    send_cmd(0x01,4,ch1); get_resp(NULL); beep_click();
    draw_ui("ADMIN", "Zabierz...", "..."); vTaskDelay(2000/portTICK_PERIOD_MS);
    draw_ui("ADMIN", "Potwierdz", "Poloz ponownie");
    while(1) { send_cmd(0x01,3,gen); if(get_resp(NULL)==0) break; vTaskDelay(50/portTICK_PERIOD_MS); }
    send_cmd(0x01,4,ch2); get_resp(NULL);
    send_cmd(0x01,3,reg);
    if(get_resp(NULL)==0) {
        uint8_t str[]={0x06, 0x01, (uint8_t)(next_enroll_id>>8), (uint8_t)(next_enroll_id&0xFF)};
        send_cmd(0x01,6,str);
        if(get_resp(NULL)==0) {
            draw_ui("SUKCES", "Zapisano!", ""); signal_ok();
            write_csv(next_enroll_id, "REJESTRACJA", 0, 0, "OK");
            next_enroll_id++; save_next_id();
        }
    } else { draw_ui("BLAD", "Nieudane", ""); signal_error(); }
    vTaskDelay(1500/portTICK_PERIOD_MS);
}

void check_background_timers() {
    // JEŚLI JEST ALARM LUB MENU JEST OTWARTE -> nie sprawdzać czasu (blokada pętli)
    if (pending_alert_id != -1 || menu_step != 0) return;

    time_t now = get_seconds();
    for(int i=0; i<MAX_USERS; i++) {
        if (users[i].user_id == -1) continue;

        // Żółta dioda jak ktoś ma przerwę
        if(users[i].status == STATUS_BREAK) gpio_set_level(PIN_LED_YELLOW, 1);
        else gpio_set_level(PIN_LED_YELLOW, 0);

        if (users[i].status == STATUS_WORKING || users[i].status == STATUS_STUDYING) {
            int current_session = (now - users[i].start_time) / 60;
            int total_today = users[i].total_worked_minutes + current_session;
            
            if (total_today >= config.day_limit) { users[i].status = STATUS_ALERT_END_DAY; pending_alert_id = users[i].user_id; return; }
            int limit = (users[i].status == STATUS_WORKING) ? config.work_limit : config.study_limit;
            if (current_session >= limit) { users[i].status = STATUS_ALERT_WORK; pending_alert_id = users[i].user_id; return; }
        }
        if (users[i].status == STATUS_BREAK) {
            int elapsed = (now - users[i].break_start_time) / 60;
            if (elapsed >= config.break_limit) { users[i].status = STATUS_ALERT_BREAK; pending_alert_id = users[i].user_id; return; }
        }
    }
}

// --- ISR ---
void IRAM_ATTR isr_btn() { static uint32_t l=0; uint32_t n=esp_timer_get_time()/1000; if(n-l>300){button_flag=true; l=n;} }
void IRAM_ATTR isr_enc() { static int8_t l=0; int8_t c=(gpio_get_level(ENC_SIA_PIN)<<1)|gpio_get_level(ENC_SIB_PIN); if((l==0&&c==2)||(l==3&&c==1)) encoder_pos++; else if((l==0&&c==1)||(l==3&&c==2)) encoder_pos--; l=c; }

// --- MAIN ---
void app_main() {
    gpio_install_isr_service(0);
    gpio_reset_pin(PIN_LED_GREEN); gpio_set_direction(PIN_LED_GREEN, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_LED_RED); gpio_set_direction(PIN_LED_RED, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_LED_YELLOW); gpio_set_direction(PIN_LED_YELLOW, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_BUZZER); gpio_set_direction(PIN_BUZZER, GPIO_MODE_OUTPUT);

    gpio_config_t io={.pin_bit_mask=(1ULL<<ENC_SW_PIN)|(1ULL<<ENC_SIA_PIN)|(1ULL<<ENC_SIB_PIN),.mode=GPIO_MODE_INPUT,.pull_up_en=1,.intr_type=3};
    gpio_config(&io);
    gpio_isr_handler_add(ENC_SW_PIN, isr_btn, NULL); gpio_isr_handler_add(ENC_SIA_PIN, isr_enc, NULL); gpio_isr_handler_add(ENC_SIB_PIN, isr_enc, NULL);

    u8g2_esp32_hal_t uh=U8G2_ESP32_HAL_DEFAULT; uh.bus.i2c.sda=I2C_SDA; uh.bus.i2c.scl=I2C_SCL; u8g2_esp32_hal_init(uh);
    u8g2_Setup_sh1106_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8g2_esp32_i2c_byte_cb, u8g2_esp32_gpio_and_delay_cb);
    u8x8_SetI2CAddress(&u8g2.u8x8, OLED_ADDR<<1); u8g2_InitDisplay(&u8g2); u8g2_SetPowerSave(&u8g2, 0);

    uart_config_t uc={57600, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1, UART_HW_FLOWCTRL_DISABLE, UART_SCLK_DEFAULT};
    uart_driver_install(UART_PORT, 1024, 0, 0, NULL, 0); uart_param_config(UART_PORT, &uc); uart_set_pin(UART_PORT, FP_TX_PIN, FP_RX_PIN, -1, -1);

    esp_vfs_fat_sdmmc_mount_config_t mc={.format_if_mount_failed=true, .max_files=5, .allocation_unit_size=16*1024};
    sdmmc_card_t *card; sdmmc_host_t host=SDSPI_HOST_DEFAULT(); host.max_freq_khz=400;
    spi_bus_config_t bc={.mosi_io_num=PIN_MOSI, .miso_io_num=PIN_MISO, .sclk_io_num=PIN_CLK, .quadwp_io_num=-1, .quadhd_io_num=-1};
    spi_bus_initialize(host.slot, &bc, SDSPI_DEFAULT_DMA);
    sdspi_device_config_t sc=SDSPI_DEVICE_CONFIG_DEFAULT(); sc.gpio_cs=PIN_CS; sc.host_id=host.slot;
    if(esp_vfs_fat_sdspi_mount("/sdcard", &host, &sc, &mc, &card)==ESP_OK) { sd_available=true; load_config(); load_next_id(); beep_click(); }

    init_users();
    signal_ok();

    while(1) {
        check_background_timers(); 

        // --- 1. ALARM (PRIORYTET) ---
        if (pending_alert_id != -1) {
            static int flash=0; flash=!flash;
            led_set(0, flash, 0); 
            if(flash && alert_beep_count < 10) { beep(2000, 100); alert_beep_count++; }

            UserSession* u = get_user(pending_alert_id);
            char msg[32]; 
            
            if (u->status == STATUS_ALERT_WORK) { sprintf(msg, "ALARM ID: %d", pending_alert_id); draw_ui(msg, "Koniec Czasu!", "Przyloz palec!"); } 
            else if (u->status == STATUS_ALERT_BREAK) { sprintf(msg, "KONIEC PRZERWY %d", pending_alert_id); draw_ui(msg, "Wracaj do pracy!", "Przyloz palec!"); }
            else if (u->status == STATUS_ALERT_END_DAY) { sprintf(msg, "KONIEC DNIA ID:%d", pending_alert_id); draw_ui(msg, "Limit Osiagniety", "Przyloz zeby wyjsc"); }

            if(button_flag) { // Aktywacja skanera
                button_flag = false;
                int scanned_id = scan_blocking();
                if (scanned_id == pending_alert_id || scanned_id == ADMIN_ID) {
                    beep_click(); led_set(0,0,0);
                    alert_beep_count = 0; // Reset beepera na przyszłość
                    
                    if (u->status == STATUS_ALERT_WORK) {
                        u->total_worked_minutes += (get_seconds() - u->start_time)/60;
                        write_csv(u->user_id, "PRACA_LIMIT", config.work_limit, config.work_limit, "PRZERWA");
                        u->status = STATUS_BREAK;
                        u->break_start_time = get_seconds();
                        u->breaks_taken_count++;
                        draw_ui("STATUS", "Rozpoczeto", "Przerwe");
                        pending_alert_id = -1; // <--- ODBLOKUJ ALARM OD RAZU!
                    }
                    else if (u->status == STATUS_ALERT_BREAK) {
                        u->total_break_minutes += (get_seconds() - u->break_start_time)/60;
                        current_user_id = (scanned_id==ADMIN_ID) ? u->user_id : scanned_id; 
                        menu_step = 100; // Pytanie o przedłużenie
                        pending_alert_id = -1; // <--- od razu odblok. alarmu! (Menu 100 przejmuje kontrolę)
                    }
                    else if (u->status == STATUS_ALERT_END_DAY) {
                        write_csv(u->user_id, "KONIEC_DNIA", u->total_worked_minutes, config.day_limit, "WYLOGOWANIE");
                        u->status = STATUS_OFFLINE;
                        remove_user(u->user_id);
                        draw_ui("DO WIDZENIA", "Koniec pracy", "");
                        pending_alert_id = -1; // <--- ODBLOKUJ
                        vTaskDelay(2000/portTICK_PERIOD_MS);
                    }
                    vTaskDelay(1000/portTICK_PERIOD_MS);
                } else { draw_ui("BLAD", "Zly Palec!", "Sprobuj ponownie"); signal_error(); vTaskDelay(1000/portTICK_PERIOD_MS); }
            }
            vTaskDelay(100/portTICK_PERIOD_MS);
            continue;
        }

        // --- 2. EXTEND MENU ---
        if (menu_step == 100) {
            UserSession* u = get_user(current_user_id);
            int enc = encoder_pos; if (enc < 0) { encoder_pos=0; enc=0; } if (enc > 1) { encoder_pos=1; enc=1; }
            draw_ui("PRZEDLUZYC?", enc==0 ? "> TAK" : "  TAK", enc==1 ? "> NIE" : "  NIE");

            if (button_flag) {
                button_flag = false;
                if (enc == 0 && u->extensions_used < config.max_extensions) {
                    u->extensions_used++;
                    u->break_start_time = get_seconds();
                    u->status = STATUS_BREAK;
                    write_csv(u->user_id, "PRZERWA_EXT", 0, config.break_limit, "PRZEDLUZONO");
                    draw_ui("OK", "Przedluzono", "");
                } else {
                    if(enc==0) { draw_ui("BLAD", "Limit osiagniety", ""); vTaskDelay(1000/portTICK_PERIOD_MS); }
                    u->status = (u->user_id == ADMIN_ID) ? STATUS_WORKING : STATUS_STUDYING; // Powrót do domyślnego
                    u->start_time = get_seconds();
                    write_csv(u->user_id, "KONIEC_PRZERWY", 0, 0, "POWROT");
                    draw_ui("OK", "Do Pracy!", "");
                }
                menu_step = 0; current_user_id = -1;
                vTaskDelay(1000/portTICK_PERIOD_MS);
            }
            continue;
        }

        // --- 3. IDLE ---
        if (current_user_id == -1) {
            int p_cnt, u_cnt; get_active_stats(&p_cnt, &u_cnt);
            char dash_buf[32]; sprintf(dash_buf, "P:%d U:%d", p_cnt, u_cnt);
            draw_ui("SYSTEM RCP", "Gotowy", dash_buf); 
            
            if (encoder_pos > 5) { encoder_pos=0; enroll_blocking(); }

            if (button_flag) {
                button_flag = false;
                draw_ui("LOGOWANIE", "Przyloz palec...", "...");
                int id = scan_blocking();
                if (id > 0) {
                    current_user_id = id; signal_ok();
                    UserSession* u = create_user(id);
                    if (id == ADMIN_ID) menu_step = 50;
                    else if (u->status == STATUS_OFFLINE) menu_step = 1;
                    else menu_step = 150; // DASHBOARD
                    encoder_pos = 0;
                } else if (id == -2) { draw_ui("BLAD", "Nieznany!", ""); signal_error(); vTaskDelay(1000/portTICK_PERIOD_MS); } 
                else beep_click();
            }
        } 
        // --- 4. USER MENU ---
        else {
            UserSession* u = get_user(current_user_id);
            int enc = encoder_pos;

            if (menu_step == 1) { // Wybór
                if(enc<0){encoder_pos=0; enc=0;} if(enc>1){encoder_pos=1; enc=1;}
                draw_ui("Witaj!", enc==0 ? "> PRACA" : "  PRACA", enc==1 ? "> NAUKA" : "  NAUKA");
                if (button_flag) {
                    button_flag = false;
                    u->start_time = get_seconds();
                    u->status = (enc==0) ? STATUS_WORKING : STATUS_STUDYING;
                    write_csv(u->user_id, (enc==0)?"START_PRACA":"START_NAUKA", 0, 0, "START");
                    signal_ok(); current_user_id = -1; menu_step = 0;
                }
            }
            else if (menu_step == 150) { // Dashboard
                time_t now = get_seconds();
                int remaining = 0;
                if (u->status == STATUS_WORKING) remaining = config.work_limit - ((now - u->start_time)/60);
                else if (u->status == STATUS_STUDYING) remaining = config.study_limit - ((now - u->start_time)/60);
                else if (u->status == STATUS_BREAK) remaining = config.break_limit - ((now - u->break_start_time)/60);
                
                draw_dashboard(u, remaining);
                if (button_flag) {
                    button_flag = false;

                    //Krótkie kliknięcie = powrót do IDLE
                    current_user_id = -1; menu_step = 0;
                }
                // Bdr
                if (encoder_pos < -2) {
                    write_csv(u->user_id, "WYLOGOWANIE_RECZNE", 0, 0, "KONIEC");
                    u->status = STATUS_OFFLINE; remove_user(current_user_id);
                    draw_ui("KONIEC", "Wylogowano", ""); signal_ok();
                    vTaskDelay(1000/portTICK_PERIOD_MS);
                    current_user_id = -1; menu_step = 0; encoder_pos = 0;
                }
            }
            else if (menu_step == 50) { // Admin
                if(enc<0){encoder_pos=0; enc=0;} if(enc>1){encoder_pos=1; enc=1;}
                draw_ui("ADMIN", enc==0 ? "> Dodaj Usera" : "  Dodaj Usera", enc==1 ? "> Wyloguj" : "  Wyloguj");
                if (button_flag) {
                    button_flag = false;
                    if(enc==0) enroll_blocking();
                    else { current_user_id = -1; menu_step = 0; }
                }
            }
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}