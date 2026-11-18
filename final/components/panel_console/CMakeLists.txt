idf_component_register(SRCS "panel_console.c" INCLUDE_DIRS "include"
    PRIV_REQUIRES klipper_client device_network board_jc4880 esp_wifi esp_http_client json esp_rom esp_timer freertos log)
