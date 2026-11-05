idf_component_register(SRCS "device_network.c" "serial_menu.c" INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_hosted esp_wifi_remote esp_wifi esp_event esp_netif
                  nvs_flash freertos log esp_timer lwip esp_driver_usb_serial_jtag vfs esp_http_client json)
