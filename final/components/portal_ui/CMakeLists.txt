idf_component_register(SRCS "portal_ui.c" "portal_network_settings.c" "portal_saver.c" "portal_core.c" INCLUDE_DIRS "include"
    REQUIRES lvgl klipper_client device_network
    PRIV_REQUIRES portal_assets board_jc4880 audio_service audio_events esp_app_format esp_hw_support esp_timer freertos heap log)

if(CONFIG_PORTAL_CORE_HD)
    target_sources(${COMPONENT_LIB} PRIVATE portal_core_clip.c)
endif()
