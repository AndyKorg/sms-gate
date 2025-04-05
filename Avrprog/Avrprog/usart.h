#ifndef USART_H
#define USART_H

#include "avrlibtypes.h"

typedef enum {
	USART0 = 0,
	USART1,
	USART_MAX
} UsartModule_t;

void usart_init(UsartModule_t usart, uint16_t baud_rate);
u08 usart_getchar(UsartModule_t usart, u08 *c);
u08 usart_putchar(UsartModule_t usart, u08 c);

#endif
