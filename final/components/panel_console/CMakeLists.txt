idf_component_register(SRCS "panel_console.c" INCLUDE_DIRS "include"
    PRIV_REQUIRES klipper_client device_network esp_http_client json esp_timer freertos log)
