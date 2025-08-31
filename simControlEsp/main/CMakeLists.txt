idf_component_register(SRCS 
                    "console.c" 
                    "main.c"
                    "ussd_cmd.c"
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
                        json
                        version
                        sms_gateway
                        ota_client
                        log_spiffs
                        spiffs_manager
                    INCLUDE_DIRS "."
                    )
