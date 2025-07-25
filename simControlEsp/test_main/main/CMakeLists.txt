idf_component_register(
                    SRCS
                            ../../esp_components/sim900d_uart/test/test_sim900d_uart.c
                            ../../esp_components/sim900d_uart/test/mocks/mock_uart.c
                            test_main.c
                      INCLUDE_DIRS "."
                      PRIV_INCLUDE_DIRS
                            "../../esp_components/sim900d_uart/include"
                            "../../esp_components/sim900d_uart/test"
                      PRIV_REQUIRES  spi_flash unity )
