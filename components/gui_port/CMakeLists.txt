idf_component_register(
    SRCS "gui_port.c" "gui_touch.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl gt911_input display_orientation
    PRIV_REQUIRES board_jc4880 esp_timer freertos heap log
)
