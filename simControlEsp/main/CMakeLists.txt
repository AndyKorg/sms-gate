idf_component_register(SRCS 
                    "console.c" 
                    "main.c"
                    "sim900d/sim900d_uart.c"
                    "sim900d/sim900d_parser.c"
                    "sim900d/sim900d_pdu.c"
                    PRIV_REQUIRES spi_flash
                    REQUIRES 
                        nvs_flash 
                        console
                        driver
                    INCLUDE_DIRS "")
