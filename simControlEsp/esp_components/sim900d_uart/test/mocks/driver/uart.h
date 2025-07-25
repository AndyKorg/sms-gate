#ifndef MOCK_DRIVER_UART_H
#define MOCK_DRIVER_UART_H

typedef int uart_port_t;
typedef int gpio_num_t;

typedef struct {
    int baud_rate;
    int data_bits;
    int parity;
    int stop_bits;
} uart_config_t;

#endif // MOCK_DRIVER_UART_H
