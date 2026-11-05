if(CONFIG_PANEL_ORIENTATION_TEST)
    idf_component_register(SRCS "app_main.c" "orientation_test.c" INCLUDE_DIRS "."
        PRIV_REQUIRES gui_port board_jc4880 esp_psram esp_system spi_flash esp_timer
                      esp_driver_usb_serial_jtag vfs freertos heap log)
else()
    idf_component_register(SRCS "app_main.c" INCLUDE_DIRS "."
        PRIV_REQUIRES gui_port board_jc4880 portal_ui device_network klipper_client panel_console audio_service audio_events
                      esp_psram esp_system spi_flash esp_timer log)
endif()
