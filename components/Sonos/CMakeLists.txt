idf_component_register(
    SRCS "src/Sonos.cpp"
    INCLUDE_DIRS "include"
    REQUIRES tinyxml2 esp_http_client
)