idf_component_register(
    SRCS 
        "test_main.c"
        "../../esp_components/sim900d_uart/test/test_sim900d_parser.c"
    INCLUDE_DIRS 
        "."
        "../../esp_components/sim900d_uart/include"    # Исправленный путь
    PRIV_REQUIRES 
        spi_flash 
        unity 
        sim900d_uart
)
