#include "serial.h"
#include <avr\io.h>

#include <util/setbaud.h>

static FILE mystdout = FDEV_SETUP_STREAM(uart_putchar, NULL,
                                             _FDEV_SETUP_WRITE);

int uart_putchar(char c, FILE *stream){
  if (c == '\n')
    uart_putchar('\r', stream);
  loop_until_bit_is_set(UCSR0A, UDRE0);
  UDR0 = c;
  return 0;
}

void SerilalIni(){

	// USART initialization
	// Communication Parameters: 8 Data, 1 Stop, No Parity
	// USART Receiver: On
	// USART Transmitter: On
	// USART Mode: Asynchronous
	// USART Baud rate: 9600

	UCSR0A = 0x00;
	UCSR0B = (1<<RXEN0) | (1<<TXEN0);
	UCSR0C = 0x06;

	UBRR0H = UBRRH_VALUE;
	UBRR0L = UBRRL_VALUE;

	stdout = &mystdout;
}

