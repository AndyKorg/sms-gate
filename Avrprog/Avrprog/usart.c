#include <avr/io.h>
#include <avr/interrupt.h>
#include "RingBuffer.h"
#include "bits_macros.h"
#include "usart.h"

#define Enable_Interrupts sei()
#define Disable_Interrupts cli()
#define IntIsOff(reg)        BitIsClear(reg, SREG_I)
#define IntIsOn(reg)         BitIsSet(reg, SREG_I)

// Буферы для приема и передачи данных для каждого USART модуля
RingBuffer_t BufTx0, BufRx0, BufTx1, BufRx1;

typedef struct {
	volatile uint8_t *UCSRnA;
	volatile uint8_t *UCSRnB;
	volatile uint8_t *UCSRnC;
	volatile uint8_t *UBRRnH;
	volatile uint8_t *UBRRnL;
	volatile uint8_t *UDRn;
	volatile uint8_t U2Xn;
	uint16_t baud_rate;
	RingBuffer_t *tx_buf;
	RingBuffer_t *rx_buf;
	volatile uint8_t status;
} UsartConfig_t;

// Массив конфигураций для всех USART
static UsartConfig_t usart_configs[USART_MAX] = {
	{&UCSR0A, &UCSR0B, &UCSR0C, &UBRR0H, &UBRR0L, &UDR0, U2X0, 57600, &BufTx0, &BufRx0, 0}, // USART0
	{&UCSR1A, &UCSR1B, &UCSR1C, &UBRR1H, &UBRR1L, &UDR1, U2X1, 57600, &BufTx1, &BufRx1, 0}, // USART1
};

// Инициализация USART
void usart_init(UsartModule_t usart, uint16_t baud_rate) {
	UsartConfig_t *config = &usart_configs[usart];
	config->baud_rate = baud_rate;
	if (config->status == 0){
		RingBuffer_init(config->rx_buf, MAX_PACKET_SIZE);
		RingBuffer_init(config->tx_buf, MAX_PACKET_SIZE);
		config->status = 1;
	}

	// Настройка скорости
	uint16_t ubrr_value = F_CPU / 16 / config->baud_rate - 1;
	*(config->UBRRnH) = (uint8_t)(ubrr_value >> 8);
	*(config->UBRRnL) = (uint8_t)ubrr_value;

	// Настройка режима USART
	*(config->UCSRnA) = 0x00;
	*(config->UCSRnB) = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);
	*(config->UCSRnC) = (0 << UMSEL01) | (0 << UMSEL00) | (0 << UPM01) | (0 << UPM00) | (0 << USBS0) | (1 << UCSZ01) | (1 << UCSZ00);

	// Если используется умножение скорости, включаем его
	#if USE_2X
	*(config->UCSRnA) |= (1 << config->U2Xn);
	#else
	*(config->UCSRnA) &= ~(1 << config->U2Xn);
	#endif
}

// Получить символ из кольцевого буфера
uint8_t usart_getchar(UsartModule_t usart, uint8_t *c) {
	UsartConfig_t *config = &usart_configs[usart];
	uint8_t ret, storeInt;

	storeInt = SREG;
	Disable_Interrupts;
	if (!RingBuffer_is_empty(config->rx_buf)) {
		*c = RingBuffer_front(config->rx_buf);
		RingBuffer_pop(config->rx_buf);
		ret = 0;
		} else {
		ret = -1;
	}
	if (IntIsOn(storeInt)) Enable_Interrupts;
	return ret;
}

// Отправить символ в кольцевой буфер
uint8_t usart_putchar(UsartModule_t usart, uint8_t c) {
	UsartConfig_t *config = &usart_configs[usart];
	uint8_t ret, storeInt;

	storeInt = SREG;
	Disable_Interrupts;
	if (!RingBuffer_is_full(config->tx_buf)) {
		RingBuffer_push(config->tx_buf, c);
		SetBit(*(config->UCSRnB), UDRIE0);
		ret = 0;
		} else {
		ret = -1;
	}
	if (IntIsOn(storeInt)) Enable_Interrupts;
	return ret;
}

// Отправка строки
void usart_send_string(UsartModule_t usart, const char* str) {
	while (*str) {
		while (usart_putchar(usart, *str) != 0); // Ждём, если буфер заполнен
		str++;
	}
}

// Прием строки до символа '\n' или '\r', либо пока не достигнут max_len - 1
uint8_t usart_receive_string(UsartModule_t usart, char* buffer, uint8_t max_len) {
	uint8_t i = 0;
	uint8_t c;

	while (i < (max_len - 1)) {
		if (usart_getchar(usart, &c) == 0) {
			if (c == '\n' || c == '\r') {
				break;
			}
			buffer[i++] = c;
		}
	}
	buffer[i] = '\0'; // Завершаем строку
	return i;
}

// Инлайн функции для обработки прерываний для каждого USART модуля
static inline void usart_rx_interrupt(UsartModule_t usart) {
	unsigned char rxbyte;
	UsartConfig_t *config = &usart_configs[usart];

	rxbyte = *(config->UDRn);  // Чтение полученного байта
	if (!RingBuffer_is_full(config->rx_buf)) {
		RingBuffer_push(config->rx_buf, rxbyte);  // Запись в буфер приема
	}
}

static inline void usart_udre_interrupt(UsartModule_t usart) {
	UsartConfig_t *config = &usart_configs[usart];

	if (RingBuffer_is_empty(config->tx_buf)) {
		ClearBit(*(config->UCSRnB), UDRIE0);  // Если буфер передачи пуст, останавливаем прерывание
		} else {
		char txbyte = RingBuffer_front(config->tx_buf);  // Извлечение байта для передачи
		RingBuffer_pop(config->tx_buf);
		*(config->UDRn) = txbyte;  // Передача байта
	}
}

// Прерывания для каждого USART модуля
ISR(USART0_RX_vect) {
	usart_rx_interrupt(USART0);
}

ISR(USART0_UDRE_vect) {
	usart_udre_interrupt(USART0);
}

ISR(USART1_RX_vect) {
	usart_rx_interrupt(USART1);
}

ISR(USART1_UDRE_vect) {
	usart_udre_interrupt(USART1);
}
