idf_component_register(
    SRCS "board_display.c" "board_touch.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_ledc
    PRIV_REQUIRES esp_lcd esp_hw_support st7701_sequence freertos log
)
