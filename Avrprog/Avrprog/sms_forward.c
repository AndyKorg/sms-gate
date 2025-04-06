#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <util/delay.h>
#include "usart.h"

#include "sms_forward.h"

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
    uint8_t res = usart_receive_string(gsm_usart_local, buffer, max_len, 8000);

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
    const int max_len = 64;
    char buffer[max_len];
    int len = strlen(expected);
    if (len >= max_len) {
        return 1;
    }
    uint16_t waited = 0;
    const uint16_t poll_interval = 50;

    while (waited < timeout_ms) {
        if (gsm_read_line(buffer, len+1)) {
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

void gsm_log_bytes_sent(const char *label, const uint8_t *data, size_t len)
{
    printf("[TX] %s (%zu bytes): ", label, len);
    for (size_t i = 0; i < len; ++i) {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

void gsm_log_bytes_received(const char *label, const uint8_t *data, size_t len)
{
    printf("[RX] %s (%zu bytes): ", label, len);
    for (size_t i = 0; i < len; ++i) {
        printf("%02X ", data[i]);
    }
    printf("\n");
}


// Пересылка входящего SMS-сообщения
static void gsm_forward_sms(const char* message)
{
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

    if (!gsm_send_command("AT")) return;
    if (!gsm_send_command("AT+CMGF=1")) return;
    if (!gsm_send_command("AT+CNMI=2,2,0,0,0")) return;

    gsm_log("[INIT] SMS forwarding initialized");

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

#define MAX_RESET_ATTEMPTS 10

int sim_reset(PwrKeyControlCallback pwr_callback)
{
    gsm_log("[SIM900] Start reset...");
    if (gsm_usart_local >= USART_MAX || pwr_callback == NULL) {
        return -1;
    }

    uint8_t resets = 0;
    uint8_t rx_buffer[8];

    while (resets < MAX_RESET_ATTEMPTS) {
        // Сброс через пин управления
        pwr_callback(1);
        _delay_ms(1500);  // удержание
        pwr_callback(0);
        _delay_ms(3200);  // ожидание загрузки

        gsm_log("[SIM900] Waiting for power-up pattern...");

        uint16_t waited = 0;
        uint16_t timeout = 10000;
        uint8_t byte;
        uint8_t idx = 0;

        // Ожидаем 2–6 байт, последние два из которых должны быть 0xFF
        while (waited < timeout && idx < sizeof(rx_buffer)) {
            if (usart_getchar(gsm_usart_local, &byte)) {
                rx_buffer[idx++] = byte;

                // Проверка: есть ли 2 байта и последние два — 0xFF
                if (idx >= 2 &&
                        rx_buffer[idx - 1] == 0xFF &&
                        rx_buffer[idx - 2] == 0xFF) {

                    char msg[64];
                    snprintf(msg, sizeof(msg), "[SIM900] Got power pattern (%d bytes)", idx);
                    gsm_log(msg);
                    break;
                }
            } else {
                _delay_ms(50);
                waited += 50;
            }
        }

        if (idx < 2 || rx_buffer[idx - 1] != 0xFF || rx_buffer[idx - 2] != 0xFF) {
            gsm_log("[SIM900] Invalid power-up pattern, retrying...");
            _delay_ms(1000);
            resets++;
            continue;
        }

        // После этого может прийти строка, например, "NORMAL POWER DOWN" в течении 500 мс
/*
        if (gsm_wait_for_response("NORMAL POWER DOWN", 500)) {
            gsm_log("[SIM900] Received NORMAL POWER DOWN, retrying...");
            resets++;
            continue;
        }
*/
        // Проверка связи через AT
        usart_send_string(gsm_usart_local, "AT\r");
        if (gsm_wait_for_response("OK", 2000)) {
            gsm_log("[SIM900] Module ready after reset");
            return 0;
        }

        gsm_log("[SIM900] No OK after AT, retrying reset...");
        resets++;
    }

    gsm_log("[SIM900] All reset attempts failed");
    return -1;
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

    return 0;
}