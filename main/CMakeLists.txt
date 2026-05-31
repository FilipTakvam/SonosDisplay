idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES
                        Wifi_Driver
                        Wifi_Config
                        Sonos
                        MatrixDisplay
                        nvs_flash
                    )