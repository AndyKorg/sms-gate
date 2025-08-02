idf_component_register(SRCS "sim900d_uart.c" "sim900d_handlers.c"
                      "sim900d_parser.c"
                      "sim900d_pdu.c"
                     
                      INCLUDE_DIRS "include"
                      PRIV_INCLUDE_DIRS "."
                      REQUIRES driver
)