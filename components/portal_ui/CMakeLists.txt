idf_component_register(SRCS "portal_ui.c" "portal_network_settings.c" INCLUDE_DIRS "include"
    REQUIRES lvgl klipper_client device_network
    PRIV_REQUIRES portal_assets board_jc4880 audio_service audio_events esp_timer freertos log)
