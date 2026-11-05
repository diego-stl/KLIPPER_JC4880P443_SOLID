idf_component_register(SRCS "audio_service.c" "audio_assets.c" INCLUDE_DIRS "include"
    PRIV_REQUIRES board_jc4880 esp_codec_dev esp_driver_i2s esp_driver_gpio
                  freertos esp_timer nvs_flash log)
