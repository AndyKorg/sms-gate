#ifndef USSD_CMD_H
#define USSD_CMD_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief Инициализация модуля USSD команд
 * @return ESP_OK в случае успеха
 */
esp_err_t ussd_cmd_init(void);

/**
 * @brief Запуск задачи проверки баланса
 * @param delay_ms Задержка перед выполнением проверки в миллисекундах
 * @return ESP_OK в случае успеха
 */
esp_err_t ussd_cmd_start_balance_check(uint32_t delay_ms);

/**
 * @brief Остановка модуля USSD команд
 */
void ussd_cmd_deinit(void);

#endif // USSD_CMD_H