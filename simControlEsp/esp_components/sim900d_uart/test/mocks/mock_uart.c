#include "mock_uart.h"
#include "unity.h"

static int expected_ret = 0;

void uart_write_bytes_ExpectAndReturn(int uart_num, const char *data, size_t len, int ret) {
    expected_ret = ret;
}

int uart_write_bytes(int uart_num, const char *data, size_t len) {
    return expected_ret;
}

void uart_read_bytes_ExpectAnyArgsAndReturn(int ret) {
    expected_ret = ret;
}

int uart_read_bytes(int uart_num, uint8_t *data, size_t len, int timeout) {
    return expected_ret;
}
