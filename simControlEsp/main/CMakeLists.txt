idf_component_register(SRCS 
                    "console.c" 
                    "main.c"
                    "sim900d_uart"
                    PRIV_REQUIRES spi_flash
                    REQUIRES 
                        nvs_flash 
                        console
                        driver
                    INCLUDE_DIRS "")
