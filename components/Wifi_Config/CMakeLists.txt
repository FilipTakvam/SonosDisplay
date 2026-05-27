idf_component_register(
    SRCS "src/Wifi_Config.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_http_server nvs_flash esp_netif esp_event mdns
)