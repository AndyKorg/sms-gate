#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "usart.h"

#define USART_EXT		USART0
#define USART_SIM900	USART1

#include "bits_macros.h"

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

#include "sms_forward.h"

void Sim900SReset(void){
	uint8_t i, St;
	
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
				usart_putchar(USART_EXT,'O');
				usart_putchar(USART_EXT,'k');
				St = 99;
				break;
			}
		}
		else{
			if (StatusIsReady){
				usart_putchar(USART_EXT,'O');
				usart_putchar(USART_EXT,'k');
				St = 99;
				break;
			}
		}
	}
	if (St != 99){
		usart_putchar(USART_EXT,'E');
		usart_putchar(USART_EXT,'r');
		usart_putchar(USART_EXT,'r');
	}
	usart_putchar(USART_EXT,0xD);
	usart_putchar(USART_EXT,0xA);
}

int main(void){

	PWR_KEY_DDR_PORT |= Bit(PWR_KEY_PIN);
	PwrKeyOff;
	//_delay_ms(500);										//Задержка перед включением модуля
	
	//-------- Настройка порта для компьютера
	usart_init(USART_EXT, 57600);
	//-------- Настройка порта для модуля SIM900
	usart_init(USART_SIM900, 57600);

	Enable_Interrupts;
	
	usart_putchar(USART_EXT, 'R');						//Микроконтроллер готов
	Sim900SReset();

	if (sms_forward_init(USART_SIM900, "+79697120710", USART_EXT) == 0){
		gsm_wait_and_forward_sms();
	}
    while(1);
}
