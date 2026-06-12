#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "config.h"
#include "common.h"
#include "rtc.h"

void write_csv(int id, const char *act, int dur, int lim, const char *stat) {
    if (!sd_available) return;

    char date[32];
    get_full_date(date);

    FILE *f = fopen("/sdcard/data.csv", "a");
    if (f) {
        if (stat == NULL) stat = "INFO";
        fprintf(f, "%s;%d;%s;%d;%d;%s\n", date, id, act, dur, lim, stat);
        fclose(f);
    }
}

void load_config(void) {
    FILE *f = fopen("/sdcard/config.txt", "r");

    // Domyślne wartości (zabezpieczenie, gdy plik nie istnieje)
    config.work_limit = 60;
    config.study_limit = 45;
    config.break_limit = 15;
    config.max_extensions = 2;
    config.day_limit = 480;

    int set_time_flag = 0;
    char datetime_buf[64] = "2000-01-01 00:00:00";

    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            // usuń znak nowej linii na końcu, żeby nie śmiecił
            line[strcspn(line, "\n")] = 0;

            char *val_ptr = strchr(line, '=');
            if (val_ptr) {
                val_ptr++; // wskaźnik za znak '='

                if (strstr(line, "work")) config.work_limit = atoi(val_ptr);
                else if (strstr(line, "study")) config.study_limit = atoi(val_ptr);
                else if (strstr(line, "break")) config.break_limit = atoi(val_ptr);
                else if (strstr(line, "max_ext")) config.max_extensions = atoi(val_ptr);
                else if (strstr(line, "day_limit")) config.day_limit = atoi(val_ptr);
                else if (strstr(line, "set_time")) set_time_flag = atoi(val_ptr);
                else if (strstr(line, "datetime")) strcpy(datetime_buf, val_ptr);
            }
        }
        fclose(f);
    }

    // --- LOGIKA USTAWIANIA CZASU ---
    if (set_time_flag == 1) {
        int Y, M, D, h, m, s;
        if (sscanf(datetime_buf, "%d-%d-%d %d:%d:%d", &Y, &M, &D, &h, &m, &s) == 6) {
            set_rtc_time(Y, M, D, h, m, s);
            beep(1000, 100); beep(1000, 100); // Podwójny bip = sukces
        }

        // Nadpisanie pliku, żeby zmienić set_time na 0
        // (zegar nie cofnie się przy każdym restarcie, tylko gdy
        // użytkownik tego chce, ustawiając set_time=1)
        f = fopen("/sdcard/config.txt", "w");
        if (f) {
            fprintf(f, "work=%d\n", config.work_limit);
            fprintf(f, "study=%d\n", config.study_limit);
            fprintf(f, "break=%d\n", config.break_limit);
            fprintf(f, "max_ext=%d\n", config.max_extensions);
            fprintf(f, "day_limit=%d\n", config.day_limit);
            fprintf(f, "set_time=0\n");
            fprintf(f, "datetime=%s\n", datetime_buf); // info, stara data
            fclose(f);
            printf("CONFIG: Zaktualizowano plik (set_time=0)\n");
        }
    }
}

void load_next_id(void) {
    if (!sd_available) { next_enroll_id = 2; return; }

    FILE *f = fopen("/sdcard/last_id.txt", "r");
    if (f) {
        fscanf(f, "%d", &next_enroll_id);
        fclose(f);
        if (next_enroll_id < 2) next_enroll_id = 2;
    } else {
        next_enroll_id = 2;
    }
}

void save_next_id(void) {
    if (!sd_available) return;

    FILE *f = fopen("/sdcard/last_id.txt", "w");
    if (f) {
        fprintf(f, "%d", next_enroll_id);
        fclose(f);
    }
}