idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES 
                        Wifi_Driver
                        Sonos
                        nvs_flash
                        ESP32-HUB75-MatrixPanel-I2S-DMA
                    )