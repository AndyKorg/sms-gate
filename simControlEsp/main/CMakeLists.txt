idf_component_register(SRCS 
                    "console.c" 
                    "main.c"
                    PRIV_REQUIRES spi_flash
                    REQUIRES 
                        nvs_flash 
                        console
                        driver
                        sim900d_uart
                        wifi_start
                    INCLUDE_DIRS ".")
