#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <util/delay.h>
#include "usart.h"

static UsartModule_t gsm_usart_local = USART_MAX;
static UsartModule_t log_usart_local = USART_MAX;

#define PHONE_NUMBER_MAX_LEN 32
static char phone_number_to[PHONE_NUMBER_MAX_LEN] = "";

// ===== Логирование через второй USART =====
static void gsm_log(const char* msg) {
	if (log_usart_local < USART_MAX) {
		usart_send_string(log_usart_local, msg);
		usart_send_string(log_usart_local, "\r\n");
	}
}

// Чтение строки от GSM-модуля (до \n или пока не выйдет по длине)
static uint8_t gsm_read_line(char* buffer, uint8_t max_len) {
	uint8_t res = usart_receive_string(gsm_usart_local, buffer, max_len);
	if (res && log_usart_local < USART_MAX) {
		gsm_log("[GSM] << ");
		gsm_log(buffer);
	}
	return res;
}

// Ожидание строки ответа от модуля
static uint8_t gsm_wait_for_response(const char* expected, uint16_t timeout_ms) {
	char buffer[64];
	uint16_t waited = 0;
	const uint16_t poll_interval = 50;

	while (waited < timeout_ms) {
		if (gsm_read_line(buffer, sizeof(buffer))) {
			if (strstr(buffer, expected)) {
				gsm_log("[GSM] Response OK");
				return 1;
			}
		}
		_delay_ms(poll_interval);
		waited += poll_interval;
	}
	gsm_log("[GSM] Response timeout");
	return 0;
}

// Отправка AT-команды и ожидание "OK"
static uint8_t gsm_send_command(const char* cmd) {
	gsm_log("[GSM] >> ");
	gsm_log(cmd);

	usart_send_string(gsm_usart_local, cmd);
	usart_send_string(gsm_usart_local, "\r");
	return gsm_wait_for_response("OK", 2000);
}

// Пересылка входящего SMS-сообщения
static void gsm_forward_sms(const char* message) {
	gsm_log("[SMS] Forwarding message");

	if (!gsm_send_command("AT+CMGF=1")) return;

	usart_send_string(gsm_usart_local, "AT+CMGS=\"");
	usart_send_string(gsm_usart_local, phone_number_to);
	usart_send_string(gsm_usart_local, "\"\r");

	if (!gsm_wait_for_response(">", 2000)) {
		gsm_log("[SMS] No prompt '>' received");
		return;
	}

	usart_send_string(gsm_usart_local, message);
	usart_putchar(gsm_usart_local, 26); // Ctrl+Z

	gsm_wait_for_response("OK", 10000);
}

// Главная функция ожидания и пересылки SMS
void gsm_wait_and_forward_sms() {
	char line[160];

	while (1) {
		if (gsm_read_line(line, sizeof(line))) {
			if (strstr(line, "+CMT:")) {
				gsm_log("[GSM] CMT received");
				if (gsm_read_line(line, sizeof(line))) {
					gsm_log("[GSM] transfer sms");
					gsm_forward_sms(line);
				}
				else {
					gsm_log("[GSM] message not found");
				}
			}
		}
	}
}

// Инициализация пересылки SMS
int sms_forward_init(UsartModule_t gsm_usart, const char* forward_phone, UsartModule_t log_usart) {
	if (gsm_usart >= USART_MAX || forward_phone == NULL) {
		return -1;
	}
	if (strlen(forward_phone) >= PHONE_NUMBER_MAX_LEN) {
		return -1;
	}

	gsm_usart_local = gsm_usart;
	log_usart_local = log_usart;

	strncpy(phone_number_to, forward_phone, PHONE_NUMBER_MAX_LEN - 1);
	phone_number_to[PHONE_NUMBER_MAX_LEN - 1] = '\0';

	if (!gsm_send_command("AT")) return -2;
	if (!gsm_send_command("AT+CMGF=1")) return -3;
	if (!gsm_send_command("AT+CNMI=2,2,0,0,0")) return -4;

	gsm_log("[INIT] SMS forwarding initialized");

	return 0;
}
