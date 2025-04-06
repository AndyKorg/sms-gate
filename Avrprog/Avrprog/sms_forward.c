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
static void gsm_log(const char* msg)
{
    if (log_usart_local < USART_MAX) {
        usart_send_string(log_usart_local, msg);
        usart_send_string(log_usart_local, "\r\n");
    }
}

// Чтение строки от GSM-модуля (до \n или пока не выйдет по длине)
static uint8_t gsm_read_line(char* buffer, uint8_t max_len)
{
    gsm_log("[DEBUG] gsm_read_line: waiting for line...");
    uint8_t res = usart_receive_string(gsm_usart_local, buffer, max_len);

    if (res) {
        if (log_usart_local < USART_MAX) {
            char dbg[180];
            snprintf(dbg, sizeof(dbg), "[GSM] << [%d bytes]: %s", res, buffer);
            gsm_log(dbg);
        }
    } else {
        gsm_log("[DEBUG] gsm_read_line: no data received (res == 0)");
    }

    return res;
}

// Ожидание строки ответа от модуля
static uint8_t gsm_wait_for_response(const char* expected, uint16_t timeout_ms)
{
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
static uint8_t gsm_send_command(const char* cmd)
{
    gsm_log("[GSM] >> ");
    gsm_log(cmd);

    usart_send_string(gsm_usart_local, cmd);
    usart_send_string(gsm_usart_local, "\r");
    return gsm_wait_for_response("OK", 2000);
}

void gsm_log_bytes_sent(const char *label, const uint8_t *data, size_t len) {
	printf("[TX] %s (%zu bytes): ", label, len);
	for (size_t i = 0; i < len; ++i) {
		printf("%02X ", data[i]);
	}
	printf("\n");
}

void gsm_log_bytes_received(const char *label, const uint8_t *data, size_t len) {
	printf("[RX] %s (%zu bytes): ", label, len);
	for (size_t i = 0; i < len; ++i) {
		printf("%02X ", data[i]);
	}
	printf("\n");
}


// Пересылка входящего SMS-сообщения
static void gsm_forward_sms(const char* message) {
	gsm_log("[SMS] Forwarding message");

	if (!gsm_send_command("AT+CMGF=1")) {
		gsm_log("[SMS] Failed to set text mode");
		return;
	}

	// Собираем всю команду в буфер
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"\n", phone_number_to);

	gsm_log("[GSM] >> ");
	gsm_log(cmd);

	usart_send_string(gsm_usart_local, cmd);
	usart_send_string(gsm_usart_local, "\r");

	gsm_log("[DEBUG] Waiting for > prompt...");
	_delay_ms(200); // 200 мс пауза перед ожиданием >

	// Ждём приглашение к вводу сообщения
	if (!gsm_wait_for_response(">", 5000)) {
		gsm_log("[SMS] No prompt '>' received");
		return;
	}

	gsm_log("[DEBUG] Sending SMS text and Ctrl+Z...");
	usart_send_string(gsm_usart_local, message);
	usart_putchar(gsm_usart_local, 26); // Ctrl+Z

	gsm_log("[DEBUG] Waiting for final OK after sending...");
	if (!gsm_wait_for_response("OK", 10000)) {
		gsm_log("[SMS] Sending failed or timed out");
		} else {
		gsm_log("[SMS] Message sent successfully");
	}
}


// Главная функция ожидания и пересылки SMS
void gsm_wait_and_forward_sms()
{
    char line[160];

    while (1) {
        gsm_log("[DEBUG] Waiting for line...");
        if (gsm_read_line(line, sizeof(line))) {
            if (strstr(line, "+CMT:")) {
                gsm_log("[GSM] CMT received");

                gsm_log("[DEBUG] Reading message body with retries...");
                uint8_t tries = 10;
                while (tries--) {
                    if (gsm_read_line(line, sizeof(line))) {
                        gsm_log("[GSM] transfer sms");
                        gsm_forward_sms(line);
                        break;
                    }
                    _delay_ms(100); // короткая задержка между попытками
                }

                if (tries == (uint8_t)-1) {
                    gsm_log("[GSM] message not found");
                }
            }
        }
    }
}

// Инициализация пересылки SMS
int sms_forward_init(UsartModule_t gsm_usart, const char* forward_phone, UsartModule_t log_usart)
{
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
