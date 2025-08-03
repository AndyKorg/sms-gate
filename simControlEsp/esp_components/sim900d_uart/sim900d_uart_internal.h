/* @file sim900d_uart_internal.h
 * @brief Файл с внутренними функциями драйвера
*/

#ifndef SIM900D_UART_INTERNAL_H
#define SIM900D_UART_INTERNAL_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sim900d_parser.h"
#include "esp_err.h"

#include "include/sim900d_sms_types.h"

// Разрешение чтения из uart.
#define SIM900D_UART_EVENT_READ_ENABLE (1 << 0)
// Флаг успешной регистрации в сети
#define SIM900D_UART_EVENT_NET_REGISTERED (1 << 1)
// Флаг, указывающий, что драйвер занят обработкой ответа
#define SIM900D_UART_EVENT_BUSY (1 << 2)

#define SIM900D_NO_INDEX_MEM -1

/**
 * @brief Получить дескриптор группы событий для SIM900D.
 *
 * @return EventGroupHandle_t Дескриптор группы событий.
 */
EventGroupHandle_t sim900d_get_event_group(void);

/**
 * @brief Отправить AT-команду на модуль SIM900D.
 *  Если указан буфер ответа, то функция блокирует получение данных из uart другими функциями.
 *  Если не указан буфер ответа, то функция не блокирует получение данных из uart.
 *
 * @param cmd Строка AT-команды для отправки (не должна быть NULL).
 * @param response Буфер для хранения ответа (не должен быть NULL).
 * @param resp_size Размер буфера ответа.
 * @param timeout Таймаут ожидания ответа (тип TickType_t).
 * @return int 0 в случае успеха, или отрицательный код ошибки в случае неудачи.
 */
int sim900d_send_at(const char *cmd, char *response, size_t resp_size, TickType_t timeout);

/**
 * @brief Добавляет низкоприоритетную команду в очередь команд SIM900D.
 *
 * Эта функция помещает строку команды с заданным таймаутом в очередь низкоприоритетных команд.
 * Если очередь или команда не определены, возвращает false.
 *
 * Предназанчена прежде всего для команд обслуживания: проверка статуса сети, удаление прочитанных СМС и пр.
 *
 * @param cmd     Строка команды для отправки (не должна быть NULL).
 * @param timeout Таймаут ожидания отправки команды (тип TickType_t).
 * @return true, если команда успешно добавлена в очередь, иначе false.
 */
bool sim900d_enqueue_lowprio_cmd(const char *cmd, TickType_t timeout);

/** @brief Отправить полученную SMS в очередь для обработки.
 *
 * @param sms Строка SMS для отправки в очередь (не должна быть NULL)
 *
 */
void sim900d_enqueue_sms(const sms_message_t *sms);

/** @brief Отправить событие об изменении статуса сети в callback-функцию.
 *
 * @param registered Статус сети (true - зарегистрирована, false - не зарегистрирована).
 *
 * @return void
 */
void sim900d_notify_network_status(bool registered);

#endif // SIM900D_UART_INTERNAL_H
