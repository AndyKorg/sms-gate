#ifndef USART_H
#define USART_H

#include <stdint.h>

#define MAX_PACKET_SIZE	128

typedef enum {
	USART0 = 0,
	USART1,
	USART_MAX
} UsartModule_t;

void usart_init(UsartModule_t usart, uint16_t baud_rate);
uint8_t usart_getchar(UsartModule_t usart, uint8_t *c);
uint8_t usart_putchar(UsartModule_t usart, uint8_t c);
void usart_send_string(UsartModule_t usart, const char* str);
uint8_t usart_receive_string(UsartModule_t usart, char* buffer, uint8_t max_len);

#endif
