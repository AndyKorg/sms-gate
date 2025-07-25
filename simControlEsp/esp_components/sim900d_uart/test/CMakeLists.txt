idf_component_register(
    SRCS "test_sim900d_uart.c"
    INCLUDE_DIRS "mocks"
    PRIV_INCLUDE_DIRS "../include"
    REQUIRES sim900d_uart
)
