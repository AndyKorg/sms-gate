#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "avrlibtypes.h"
#include "bits_macros.h"
#include "FIFO.h"

#define PWR_KEY_DDR_PORT	DDRD
#define PWR_KEY_PORT		PORTD
#define PWR_KEY_PIN			PORTD7

#define PwrKeyOn			SetBit(PWR_KEY_PORT, PWR_KEY_PIN)
#define PwrKeyOff			ClearBit(PWR_KEY_PORT, PWR_KEY_PIN)

#define STATUS_PORT			PIND
#define STATUS_PIN			PORTD5
#define StatusIsReady		BitIsSet(STATUS_PORT, STATUS_PIN)
#define StatusIsNotReady	BitIsClear(STATUS_PORT, STATUS_PIN)

#define Enable_Interrupts	sei()
#define Disable_Interrupts	cli()
#define IntIsOff(reg)		BitIsClear(reg, SREG_I)
#define IntIsOn(reg)		BitIsSet(reg, SREG_I)

FIFO(128)	
		BufForExtReciv,
		BufForExtTrans,
		BufForSIM900Reciv,
		BufForSIM900Trans;
	

/************************************************************************/
/* Прием-передача байта от внешнего порта                               */
/************************************************************************/
ISR(USART0_RX_vect){
  unsigned char rxbyte = UDR0;
  if( !FIFO_IS_FULL(BufForExtReciv) ) {
	  FIFO_PUSH(BufForExtReciv, rxbyte);
  }
}

//Передатчик пуст
ISR(USART0_UDRE_vect){
	if( FIFO_IS_EMPTY(BufForExtTrans) ) {		//если данных в fifo больше нет то запрещаем это прерывание
		ClearBit(UCSR0B, UDRIE0);
	}
	else {										//иначе передаем следующий байт
		char txbyte = FIFO_FRONT(BufForExtTrans);
		FIFO_POP(BufForExtTrans);
		UDR0 = txbyte;
	}
}

u08 ext_getchar(u08 *c)
{
	u08 ret, storeInt;
	
	storeInt = SREG;
	Disable_Interrupts;							//запрещаем прерывания
	if( !FIFO_IS_EMPTY(BufForExtReciv) ) {		//если в буфере есть данные, то извлекаем их
		*c = FIFO_FRONT(BufForExtReciv);
		FIFO_POP(BufForExtReciv);
		ret = 0;
	}
	else {
		ret = -1; //данных нет
	}
	if IntIsOn(storeInt)
		Enable_Interrupts; //разрешаем прерывания
	return ret;
}

u08 ext_putchar(u08 c){
	u08 ret, storeInt;
		
	storeInt = SREG;
	Disable_Interrupts;							//запрещаем прерывания
	if( !FIFO_IS_FULL(BufForExtTrans) ) {		//если в буфере есть место, то добавляем туда байт
		FIFO_PUSH(BufForExtTrans, c );
		SetBit(UCSR0B, UDRIE0);					//и разрешаем прерывание по освобождению передатчика
		ret = 0;
	}
	else {
		ret = -1; //буфер переполнен
	}
	if IntIsOn(storeInt)
		Enable_Interrupts; //разрешаем прерывания
	return ret;
}

/************************************************************************/
/* Прием-передача байта от SIM900		                                */
/************************************************************************/
ISR(USART1_RX_vect){
	unsigned char rxbyte = UDR1;
	if( !FIFO_IS_FULL(BufForSIM900Reciv) ) {
		FIFO_PUSH(BufForSIM900Reciv, rxbyte);
	}
}

//Передатчик пуст
ISR(USART1_UDRE_vect){
	if( FIFO_IS_EMPTY(BufForSIM900Trans) ) {		//если данных в fifo больше нет то запрещаем это прерывание
		ClearBit(UCSR1B, UDRIE1);
	}
	else {											//иначе передаем следующий байт
		char txbyte = FIFO_FRONT(BufForSIM900Trans);
		FIFO_POP(BufForSIM900Trans);
		UDR1 = txbyte;
	}
}

u08 sim_getchar(u08 *c)
{
	u08 ret, storeInt;
	
	storeInt = SREG;
	Disable_Interrupts;								//запрещаем прерывания
	if( !FIFO_IS_EMPTY(BufForSIM900Reciv) ) {		//если в буфере есть данные, то извлекаем их
		*c = FIFO_FRONT(BufForSIM900Reciv);
		FIFO_POP(BufForSIM900Reciv);
		ret = 0;
	}
	else {
		ret = -1;									//данных нет
	}
	if IntIsOn(storeInt)
		Enable_Interrupts;							//разрешаем прерывания
	return ret;
}

u08 sim_putchar(u08 c){
	u08 ret, storeInt;
	
	storeInt = SREG;
	Disable_Interrupts;								//запрещаем прерывания
	if( !FIFO_IS_FULL(BufForSIM900Trans) ) {		//если в буфере есть место, то добавляем туда байт
		FIFO_PUSH(BufForSIM900Trans, c );
		SetBit(UCSR1B, UDRIE1);						//и разрешаем прерывание по освобождению передатчика
		ret = 0;
	}
	else {
		ret = -1; //буфер переполнен
	}
	if IntIsOn(storeInt)
		Enable_Interrupts; //разрешаем прерывания
	return ret;
}

void PwrKeySwitch(void){
	u08 i, St;
	
	if (StatusIsReady)
		St = 1;
	else
		St = 0;
	PwrKeyOn;
	_delay_ms(1000);						//Прижать на 1 секунду для включения
	PwrKeyOff;
	_delay_ms(3200);						//Через 3,2 с модуль должен быть готов
	for(i=10; i>0;i--){
		_delay_ms(200);
		if (St){
			if (StatusIsNotReady){
				ext_putchar('O');
				ext_putchar('k');
				St = 99;
				break;
			}
		}
		else{
			if (StatusIsReady){
				ext_putchar('O');
				ext_putchar('k');
				St = 99;
				break;
			}
		}
	}
	if (St != 99){
		ext_putchar('E');
		ext_putchar('r');
		ext_putchar('r');
	}
	ext_putchar(0xD);
	ext_putchar(0xA);
}

int main(void){

	u08 i;
	
	PWR_KEY_DDR_PORT |= Bit(PWR_KEY_PIN);
	PwrKeyOff;
	//_delay_ms(500);										//Задержка перед включением модуля
								
	//-------- Настройка порта для компьютера
	#define BAUD 57600									
	#include <util/setbaud.h>
	UCSR0A = 0x00;										//Все по умолчанию
	UCSR0B = (1<<RXEN0) | (1<<TXEN0) |					//Разрешить прием и передачу 
			 (1<<RXCIE0);								//и прерывание от приемника и прерывание при пустом передатчике
	UCSR0C = (0<<UMSEL01) | (0<<UMSEL00) |				//Asynchronius USART
			 (0<<UPM01) | (0<<UPM00) |					//Parity off
			 (0<<USBS0) |								//Stop bit = 1
			 (0<<UCSZ02) | (1<<UCSZ01) | (1<<UCSZ00) |	//8 бит данных
			 (0<<UCPOL0);								//Polary XCK не важно т.к. не используется в синхронном режиме
	UBRR0H = UBRRH_VALUE;
	UBRR0L = UBRRL_VALUE;
	#if USE_2X											//Если скорости не хватит подключается умножитель на 2
		UCSR0A |= (1 << U2X0);
    #else
		UCSR0A &= ~(1 << U2X0);
    #endif

	//-------- Настройка порта для модуля SIM900
	#undef BAUD											//Для пересчета скорости для модуля SIM900
	#define BAUD 57600
	#include <util/setbaud.h>
	UCSR1A = 0x00;										//Все по умолчанию
	UCSR1B = (1<<RXEN1) | (1<<TXEN1) |					//Разрешить прием и передачу
			 (1<<RXCIE1);								//и прерывание от приемника
	UCSR1C = (0<<UMSEL11) | (0<<UMSEL10) |				//Asynchronius USART
			 (0<<UPM11) | (0<<UPM10) |					//Parity off
			 (0<<USBS1) |								//Stop bit = 1
			 (0<<UCSZ12) | (1<<UCSZ11) | (1<<UCSZ10) |	//8 бит данных
			 (0<<UCPOL1);								//Polary XCK не важно т.к. не используется в синхронном режиме
	UBRR1H = UBRRH_VALUE;
	UBRR1L = UBRRL_VALUE;
	#if USE_2X											//Если скорости не хватит подключается умножитель на 2
		UCSR1A |= (1 << U2X1);
	#else
		UCSR1A &= ~(1 << U2X1);
	#endif

	Enable_Interrupts;
	
	FIFO_FLUSH(BufForExtReciv);
	FIFO_FLUSH(BufForExtTrans);
	FIFO_FLUSH(BufForSIM900Reciv);
	FIFO_FLUSH(BufForSIM900Trans);
	
	ext_putchar('R');									//Микроконтроллер готов

	ext_putchar('S');									//Старт запуска модуля SIM900
    while(1)
    {
		if (ext_getchar(&i) == 0){
			if (i == '@'){
				ext_putchar(i);
				ext_putchar(0xD);
				ext_putchar(0xA);
				PwrKeySwitch();
			}
			else{
				sim_putchar(i);
			}
		}
		if (sim_getchar(&i) == 0){
			ext_putchar(i);
		}
    }
}

/*
int uart_putc_ext(  char c, FILE *file )
{
	int ret;
	cli(); //запрещаем прерывания
	if( !FIFO_IS_FULL(BufForExtTrans) ) {		//если в буфере есть место, то добавляем туда байт
		FIFO_PUSH(BufForExtTrans, c );
		SetBit(UCSR0B, UDRIE0);					//и разрешаем прерывание по освобождению передатчика
		ret = 0;
	}
	else {
		ret = -1; //буфер переполнен
	}
	sei(); //разрешаем прерывания
	return ret;
}

int uart_getc_ext( FILE* file )
{
	int ret;
	cli();										//запрещаем прерывания
	if( !FIFO_IS_EMPTY(BufForExtReciv) ) {		//если в буфере есть данные, то извлекаем их
		ret = FIFO_FRONT( uart_rx_fifo );
		FIFO_POP( uart_rx_fifo );
	}
	else {
		ret = _FDEV_EOF; //данных нет
	}
	sei(); //разрешаем прерывания
	return ret;
}
*/
