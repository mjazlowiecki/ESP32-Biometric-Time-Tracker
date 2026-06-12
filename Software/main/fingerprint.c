#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "fingerprint.h"
#include "common.h"
#include "config.h"
#include "display.h"

// --- KOMUNIKACJA Z CZUJNIKIEM R307 (protokół binarny, UART) ---

void send_cmd(uint8_t pid, uint8_t len, uint8_t *content) {
    uart_flush_input(UART_PORT);
    uint8_t *pkt = malloc(9 + len);
    const uint8_t HEADER[2] = {0xEF, 0x01};
    const uint8_t ADDR[4] = {0xFF, 0xFF, 0xFF, 0xFF};

    memcpy(pkt, HEADER, 2);
    memcpy(pkt + 2, ADDR, 4);
    pkt[6] = pid;
    pkt[7] = len >> 8;
    pkt[8] = len;
    memcpy(pkt + 9, content, len - 2);

    uint16_t sum = pid + len;
    for (int i = 0; i < len - 2; i++) sum += content[i];
    pkt[9 + len - 2] = sum >> 8;
    pkt[9 + len - 1] = sum;

    uart_write_bytes(UART_PORT, (const char*)pkt, 9 + len);
    free(pkt);
}

uint8_t get_resp(uint8_t *d) {
    uint8_t byte;
    int timeout = 400;
    int64_t start = esp_timer_get_time() / 1000;
    bool hdr = false;

    while ((esp_timer_get_time() / 1000) - start < timeout) {
        if (uart_read_bytes(UART_PORT, &byte, 1, 10 / portTICK_PERIOD_MS) > 0) {
            if (byte == 0xEF) {
                uint8_t n;
                if (uart_read_bytes(UART_PORT, &n, 1, 20 / portTICK_PERIOD_MS) > 0 && n == 0x01) {
                    hdr = true;
                    break;
                }
            }
        }
    }
    if (!hdr) return 0xFF;

    uint8_t buf[20];
    int len = uart_read_bytes(UART_PORT, buf, 10, 100 / portTICK_PERIOD_MS);
    if (len >= 7) {
        if (d) { d[0] = 0xEF; d[1] = 0x01; memcpy(d + 2, buf, len); }
        return buf[7];
    }
    return 0xFF;
}

// --- LOGIKA WYSOKIEGO POZIOMU ---

int scan_blocking(void) {
    uint8_t gen[]  = {0x01};
    uint8_t chr[]  = {0x02, 0x01};
    uint8_t srch[] = {0x04, 0x01, 0x00, 0x00, 0x03, 0xE8};
    uint8_t rx[30];

    for (int i = 0; i < 50; i++) {
        send_cmd(0x01, 3, gen);
        if (get_resp(NULL) == 0) {
            send_cmd(0x01, 4, chr);
            if (get_resp(NULL) == 0) {
                send_cmd(0x01, 8, srch);
                get_resp(rx);
                if (rx[9] == 0) return (rx[10] << 8) | rx[11];
                if (rx[9] == 9) return -2; // odcisk nieznany
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    return -1; // timeout
}

void enroll_blocking(void) {
    uint8_t gen[] = {0x01};
    uint8_t ch1[] = {0x02, 0x01};
    uint8_t ch2[] = {0x02, 0x02};
    uint8_t reg[] = {0x05};
    char buf[20];

    sprintf(buf, "ID: %d", next_enroll_id);
    draw_ui("ADMIN", buf, "Poloz palec...");

    while (1) {
        send_cmd(0x01, 3, gen);
        if (get_resp(NULL) == 0) break;
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    send_cmd(0x01, 4, ch1);
    get_resp(NULL);
    beep_click();

    draw_ui("ADMIN", "Zabierz...", "...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    draw_ui("ADMIN", "Potwierdz", "Poloz ponownie");

    while (1) {
        send_cmd(0x01, 3, gen);
        if (get_resp(NULL) == 0) break;
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    send_cmd(0x01, 4, ch2);
    get_resp(NULL);
    send_cmd(0x01, 3, reg);

    if (get_resp(NULL) == 0) {
        uint8_t str[] = {0x06, 0x01, (uint8_t)(next_enroll_id >> 8), (uint8_t)(next_enroll_id & 0xFF)};
        send_cmd(0x01, 6, str);
        if (get_resp(NULL) == 0) {
            draw_ui("SUKCES", "Zapisano!", "");
            signal_ok();
            write_csv(next_enroll_id, "REJESTRACJA", 0, 0, "OK");
            next_enroll_id++;
            save_next_id();
        }
    } else {
        draw_ui("BLAD", "Nieudane", "");
        signal_error();
    }
    vTaskDelay(1500 / portTICK_PERIOD_MS);
}