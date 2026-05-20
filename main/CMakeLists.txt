idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES 
                        Wifi_Driver
                        Sonos
                        nvs_flash
                    )