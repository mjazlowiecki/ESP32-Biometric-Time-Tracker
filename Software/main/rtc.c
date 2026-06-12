#include <stdio.h>
#include "driver/i2c.h"
#include "rtc.h"
#include "common.h"

uint8_t bcd2dec(uint8_t val) {
    return ((val / 16 * 10) + (val % 16));
}

uint8_t dec2bcd(uint8_t val) {
    return ((val / 10 * 16) + (val % 10));
}

void get_time_str(char *buffer) {
    uint8_t reg = 0x00;
    uint8_t data[7];
    if (i2c_master_write_read_device(I2C_NUM_1, RTC_ADDR, &reg, 1, data, 7, 100) == ESP_OK) {
        sprintf(buffer, "%02d:%02d", bcd2dec(data[2] & 0x3F), bcd2dec(data[1]));
    } else {
        sprintf(buffer, "--:--");
    }
}

void get_full_date(char *buffer) {
    uint8_t reg = 0x00;
    uint8_t data[7];
    if (i2c_master_write_read_device(I2C_NUM_1, RTC_ADDR, &reg, 1, data, 7, 100) == ESP_OK) {
        sprintf(buffer, "20%02d-%02d-%02d;%02d:%02d",
                bcd2dec(data[6]), bcd2dec(data[5]), bcd2dec(data[4]),
                bcd2dec(data[2] & 0x3F), bcd2dec(data[1]));
    } else {
        sprintf(buffer, "ERR");
    }
}

time_t get_seconds(void) {
    uint8_t reg = 0x00;
    uint8_t data[7];
    if (i2c_master_write_read_device(I2C_NUM_1, RTC_ADDR, &reg, 1, data, 7, 100) == ESP_OK) {
        return (bcd2dec(data[2] & 0x3F) * 3600) + (bcd2dec(data[1]) * 60) + bcd2dec(data[0] & 0x7F);
    }
    return 0;
}

void set_rtc_time(int year, int month, int day, int hour, int minute, int second) {
    uint8_t data[8];
    data[0] = 0x00; // Start od rejestru sekund
    data[1] = dec2bcd(second);
    data[2] = dec2bcd(minute);
    data[3] = dec2bcd(hour);
    data[4] = 0x01; // Dzień tygodnia (nieużywany)
    data[5] = dec2bcd(day);
    data[6] = dec2bcd(month);
    data[7] = dec2bcd(year - 2000); // RTC trzyma rok jako 2 cyfry

    i2c_master_write_to_device(I2C_NUM_1, RTC_ADDR, data, 8, 1000 / portTICK_PERIOD_MS);
    printf("RTC: Ustawiono czas na %04d-%02d-%02d %02d:%02d:%02d\n", year, month, day, hour, minute, second);
}