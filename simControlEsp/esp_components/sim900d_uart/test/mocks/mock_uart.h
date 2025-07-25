#pragma once
#include <stdint.h>
#include <stddef.h>

int uart_write_bytes(int uart_num, const char *data, size_t len);
int uart_read_bytes(int uart_num, uint8_t *data, size_t len, int timeout);
