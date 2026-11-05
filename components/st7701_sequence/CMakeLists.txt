idf_component_register(
    SRCS "st7701_sequence.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private"
    REQUIRES esp_lcd
    PRIV_REQUIRES freertos log
)
