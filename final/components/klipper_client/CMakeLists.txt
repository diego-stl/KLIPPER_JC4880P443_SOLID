idf_component_register(SRCS "klipper_client.cpp" INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private"
    PRIV_REQUIRES device_network esp_http_client esp_websocket_client json esp_timer freertos log)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
