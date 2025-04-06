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

void pwrKeyControlCallback(uint8_t level)
{
    if (level) {
        PWR_KEY_PORT |= (1 << PWR_KEY_PIN);
    } else {
        PWR_KEY_PORT &= ~(1 << PWR_KEY_PIN);
    }
}

void usart_bridge_loop(void)
{
    uint8_t c = 0;

    while (1) {
        // USART0 -> USART1
        if (usart_getchar(USART_EXT, &c) == 0) {
            if (c=='@') {
                sim_reset(pwrKeyControlCallback);
                continue;
            }
            usart_putchar(USART_SIM900, c);
        }

        // USART1 -> USART0
        if (usart_getchar(USART_SIM900, &c) == 0) {
            usart_putchar(USART_EXT, c);
        }
        _delay_us(10);
    }
}

int main(void)
{

    PWR_KEY_DDR_PORT |= 1<<PWR_KEY_PIN;

    //-------- Настройка порта для компьютера
    usart_init(USART_EXT, 57600);
    //-------- Настройка порта для модуля SIM900
    usart_init(USART_SIM900, 57600);

    Enable_Interrupts;

	usart_send_string(USART_EXT, "start\n\r");
//debug
    if (sms_forward_init(USART_SIM900, "+79697120710", USART_EXT) == 0) {
		sim_reset(pwrKeyControlCallback);
	}

    usart_bridge_loop();
//end debug

    if (sms_forward_init(USART_SIM900, "+79697120710", USART_EXT) == 0) {
		sim_reset(pwrKeyControlCallback);
        gsm_wait_and_forward_sms();
    }

    while(1);
}
