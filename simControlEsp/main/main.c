#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <inttypes.h>
#include <stdio.h>

#include "console.h"
#include "sim900d_uart.h"

#define SIM900D_UART_NUM UART_NUM_1
#define SIM900D_UART_TX GPIO_NUM_10
#define SIM900D_UART_RX GPIO_NUM_10
#define SIM900D_PWRKEY GPIO_NUM_23
#define SIM900D_STATUS GPIO_NUM_22
#define SIM900D_RI GPIO_NUM_33

static const char *TAG = "main";

static sim900d_uart_handle_t *sim900d_handle = NULL;

static void sms_received_callback(const sms_message_t *sms) {
  printf("SMS received! Index: %d, From: %s, Text: %s\n", sms->index, sms->sender, sms->text);
}

/// @brief Проверка причины перезагрузки
static void reboot_reason_check() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    if (nvs_flash_erase() != ESP_OK || nvs_flash_init() != ESP_OK) {
      esp_deep_sleep_start();
    }
  }

  // Открытие NVS хранилища
  nvs_handle_t nvs_handle;
  if (nvs_open("storage", NVS_READWRITE, &nvs_handle) != ESP_OK) {
    esp_deep_sleep_start();
  }

  // Чтение счетчика перезагрузок
  uint32_t reboot_count = 0;
  err = nvs_get_u32(nvs_handle, "reboot_count", &reboot_count);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    reboot_count = 0; // Если ключ не найден, начинаем с 0
  } else if (err != ESP_OK) {
    esp_deep_sleep_start();
  }

  // Проверка причины перезагрузки
  esp_reset_reason_t reset_reason = esp_reset_reason();
  if (reset_reason == ESP_RST_POWERON) {
    printf("Power-on reset detected. Resetting reboot counter.\n");
    reboot_count = 0;
    if (nvs_set_u32(nvs_handle, "reboot_count", reboot_count) != ESP_OK || nvs_commit(nvs_handle) != ESP_OK) {
      esp_deep_sleep_start();
    }
  }

  // Увеличение счетчика
  reboot_count++;
  printf("Reboot count: %ld\n", reboot_count);

  // Сохранение обновленного значения в NVS
  if (nvs_set_u32(nvs_handle, "reboot_count", reboot_count) != ESP_OK || nvs_commit(nvs_handle) != ESP_OK) {
    esp_deep_sleep_start();
  }
  nvs_close(nvs_handle);

  // Проверка, достиг ли счетчик 3
  if (reboot_count >= 3) {
    printf("Reboot limit reached. Halting the chip.\n");
    esp_deep_sleep_start(); // Остановка чипа
  }
}

void app_main(void) {
  reboot_reason_check();
  console_start();

  static const uart_config_t sim900d_uart_cfg = {.baud_rate = 9600,
                                                 .data_bits = UART_DATA_8_BITS,
                                                 .parity = UART_PARITY_DISABLE,
                                                 .stop_bits = UART_STOP_BITS_1,
                                                 .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
  if (sim900d_uart_init(&sim900d_handle, SIM900D_UART_NUM, &sim900d_uart_cfg, SIM900D_UART_TX, SIM900D_UART_RX,
                        SIM900D_PWRKEY, SIM900D_STATUS, SIM900D_RI) == ESP_OK) {
    sim900d_uart_set_callback(sim900d_handle, sms_received_callback);
    ESP_LOGV(TAG, "SIM900D UART initialized");
  } else {
    ESP_LOGE(TAG, "Failed to initialize SIM900D UART");
  }
}
