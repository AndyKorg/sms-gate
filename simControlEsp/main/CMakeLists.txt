idf_component_register(SRCS 
                    "console.c" 
                    "main.c"
                    "drivers/wifi_module.c"
                    PRIV_REQUIRES spi_flash
                    REQUIRES 
                        nvs_flash 
                        console
                        driver
                        sim900d_uart
                        params
                        esp_wifi
                        http_server
                        version
                    INCLUDE_DIRS ".")
