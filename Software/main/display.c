#include <string.h>
#include <stdio.h>
#include "u8g2.h"
#include "display.h"
#include "rtc.h"

void draw_ui(const char* title, const char* content, const char* bottom) {
    u8g2_ClearBuffer(&u8g2);

    char clk[10];
    get_time_str(clk);

    u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(&u8g2, 0, 10, title);
    u8g2_DrawStr(&u8g2, 95, 10, clk);
    u8g2_DrawHLine(&u8g2, 0, 12, 128);

    // Większa czcionka dla krótkich treści, mniejsza żeby zmieścić dłuższe komunikaty
    if (strlen(content) < 12) {
        u8g2_SetFont(&u8g2, u8g2_font_ncenB14_tr);
    } else {
        u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
    }
    u8g2_DrawStr(&u8g2, 0, 40, content);

    u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(&u8g2, 0, 60, bottom);

    u8g2_SendBuffer(&u8g2);
}

void draw_dashboard(UserSession* u, int remaining_min) {
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);

    u8g2_DrawStr(&u8g2, 0, 10, "DASHBOARD");
    u8g2_DrawHLine(&u8g2, 0, 12, 128);

    char buf[32];
    sprintf(buf, "Do przerwy: %d m", remaining_min);
    u8g2_DrawStr(&u8g2, 0, 25, buf);

    sprintf(buf, "Przerw: %d", u->breaks_taken_count);
    u8g2_DrawStr(&u8g2, 0, 35, buf);

    sprintf(buf, "Czas przerw: %d m", u->total_break_minutes);
    u8g2_DrawStr(&u8g2, 0, 45, buf);

    u8g2_DrawStr(&u8g2, 0, 60, "Kliknij -> Wyjdz");
    u8g2_SendBuffer(&u8g2);
}