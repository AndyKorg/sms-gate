idf_component_register(
    SRCS 
        "test_main.c"
        "../../esp_components/sim900d_uart/test/test_sim900d_parser.c"
    INCLUDE_DIRS 
        "."
        "../../esp_components/sim900d_uart/include"    # Исправленный путь
    REQUIRES
        unity
        driver
        nvs_flash
    PRIV_REQUIRES 
        spi_flash 
        sim900d_uart
        esp_timer
)