idf_component_register(
    SRCS "gt911_input.c"
    INCLUDE_DIRS "include"
    REQUIRES display_orientation
    PRIV_REQUIRES board_jc4880 esp_lcd esp_lcd_touch_gt911 esp_lcd_touch esp_timer log
)
