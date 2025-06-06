#ifndef SIM900D_UART_H
#define SIM900D_UART_H

#include "driver/uart.h"

/**
 * @brief Структура, представляющая SMS-сообщение.
 *
 * Эта структура содержит информацию о полученном SMS-сообщении,
 * включая его индекс в памяти, номер отправителя и текст сообщения.
 *
 * @typedef sms_message_t
 * @param index   Индекс SMS-сообщения в памяти.
 * @param sender  Нуль-терминированная строка с номером телефона отправителя (до 31 символа).
 * @param text    Нуль-терминированная строка с текстом SMS-сообщения (до 160 символов).
 */
typedef struct {
  int index;
  char sender[32];
  char text[161];
} sms_message_t;

/**
 * @brief Тип функции обратного вызова для обработки входящих SMS-сообщений.
 *
 * Этот тип определяет функцию, которая вызывается при получении нового SMS-сообщения.
 *
 * @param sms Указатель на структуру sms_message_t, содержащую данные полученного SMS-сообщения.
 */
typedef void (*sms_callback_t)(const sms_message_t *sms);

/**
 * @brief Устанавливает функцию обратного вызова для событий SMS на SIM900D UART.
 *
 * Эта функция назначает пользовательскую функцию обратного вызова, которая будет вызываться
 * при получении SMS-сообщения на указанном дескрипторе SIM900D UART.
 *
 * @param cb     Функция обратного вызова для обработки событий SMS.
 */
void sim900d_uart_set_callback(sms_callback_t cb);


void sim900d_network_start(int attmpt_count);

/**
 * @brief Проверяет работоспособность SIM900D отправкой команды AT.
 *
 * @param timeout_ms Таймаут ожидания ответа, мс.
 * @return true если модуль отвечает "OK", false — если нет ответа или ошибка.
 */
bool sim900d_check_alive(uint32_t timeout_ms);

/**
 * @brief Жесткий сброс модуля SIM900D с помощью пина PWRKEY и чтение состояния пина STATUS.
 *
 * @param timeout_ms Таймаут ожидания ответа, мс.
 *
 * @return true если модуль успешно включён, false — если не удалось включить.
 */
bool sim900d_reset(const uint32_t timeout_ms);

int sim900d_uart_autobaud(uint32_t timeout_ms);

/**
 * @brief Деинициализирует UART для SIM900D и освобождает связанные ресурсы.
 *
 * Эта функция удаляет драйвер UART, освобождает очередь SMS (если она была создана),
 * а также освобождает память, выделенную под структуру handle.
 *
 */
void sim900d_uart_deinit();

/**
 * @brief Инициализация UART-интерфейса SIM900D.
 *
 * Эта функция настраивает и инициализирует UART-интерфейс для связи с модулем SIM900D.
 *
 * @param[in] uart_num Номер используемого UART-порта.
 * @param[in] uart_config Указатель на структуру конфигурации UART.
 * @param[in] txd_pin GPIO-номер для вывода TXD UART.
 * @param[in] rxd_pin GPIO-номер для вывода RXD UART.
 * @param[in] pwrkey_pin GPIO-номер для управления PWRKEY SIM900D.
 * @param[in] status_pin GPIO-номер для чтения STATUS SIM900D.
 *
 * @return
 *      - ESP_OK при успехе
 *      - Соответствующий код ошибки esp_err_t в случае неудачи
 */
esp_err_t sim900d_uart_init(uart_port_t uart_num, const uart_config_t *uart_config,
                            gpio_num_t txd_pin, gpio_num_t rxd_pin, gpio_num_t pwrkey_pin, gpio_num_t status_pin,
                            gpio_num_t ri_pin);

#endif // SIM900D_UART_H