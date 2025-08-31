#include "ussd_cmd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sim900d_ussd.h"

static const char *TAG = "ussd_cmd";
static TaskHandle_t balance_task_handle = NULL;

/**
 * @brief Задача проверки баланса
 */
static void balance_check_task(void *pvParameters) {
  uint32_t delay_ms = (uint32_t)(uintptr_t)pvParameters;

  vTaskDelay(pdMS_TO_TICKS(delay_ms));
  ESP_LOGV(TAG, "🔍 Запуск проверки баланса");

  esp_err_t ret = sim900d_ussd_get_balance();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "❌ Ошибка запроса баланса: %s", esp_err_to_name(ret));
  }

  // Очищаем handle и удаляем задачу
  balance_task_handle = NULL;
  vTaskDelete(NULL);
}

esp_err_t ussd_cmd_init(void) {

  esp_log_level_set(TAG, LOG_LOCAL_LEVEL);
  sim900d_ussd_register_params();
  sim900d_ussd_load_all_params();

  return ESP_OK;
}

esp_err_t ussd_cmd_start_balance_check(uint32_t delay_ms) {
  // Проверяем, что задача еще не запущена
  if (balance_task_handle != NULL) {
    ESP_LOGV(TAG, "⚠️ Задача проверки баланса уже запущена");
    return ESP_OK;
  }

  BaseType_t result = xTaskCreate(balance_check_task, "balance_check", 2048*2,
                                  (void *)(uintptr_t)delay_ms, // Передаем задержку как параметр
                                  2, &balance_task_handle);

  if (result == pdPASS) {
    return ESP_OK;
  } else {
    ESP_LOGE(TAG, "❌ Ошибка создания задачи проверки баланса");
    balance_task_handle = NULL;
    return ESP_FAIL;
  }
}

void ussd_cmd_deinit(void) {
  if (balance_task_handle != NULL) {
    vTaskDelete(balance_task_handle);
    balance_task_handle = NULL;
  }
  sim900d_ussd_deinit();
}