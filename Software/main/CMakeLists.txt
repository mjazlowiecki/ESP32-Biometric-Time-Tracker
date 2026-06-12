idf_component_register(
    SRCS
        "main.c"
        "rtc.c"
        "fingerprint.c"
        "display.c"
        "session.c"
        "config.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_timer
        fatfs
        sdmmc
        u8g2 
)