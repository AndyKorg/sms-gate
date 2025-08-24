#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <inttypes.h>
#include <stdio.h>

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_ERROR
#include "esp_log.h"

#include "console.h"
#include "drivers/wifi_module.h"
#include "http_srv.h"
#include "params.h"
#include "sim900d_uart.h"
#include "sms_assembler.h"
#include "sms_telegram_handler.h"
#include "telegram_bot_nvs.h"
#include "version.h"
#include "ota_client.h"

#include "ussd_cmd.h"

#define SIM900D_UART_NUM UART_NUM_1
#define SIM900D_UART_TX GPIO_NUM_17
#define SIM900D_UART_RX GPIO_NUM_16
#define SIM900D_PWRKEY GPIO_NUM_23
#define SIM900D_STATUS GPIO_NUM_22
#define SIM900D_RI GPIO_NUM_33

#define VERSION_PARAM "simCtrl_ver" // version application parameter name on the http-page
#define TELERGAMM_TEST_CMD "tlg_test"

// OTA Configuration
#define OTA_CHECK_INTERVAL_HOURS 24        // Интервал проверки обновлений в часах
#define OTA_TASK_PRIORITY 1                // Самый низкий приоритет
#define OTA_TASK_STACK_SIZE 4096           // Размер стека для задачи OTA

static const char *TAG = "main";

// Глобальные переменные для OTA
static TaskHandle_t ota_task_handle = NULL;

/**
 * Диспетчер шлюза
 */
esp_err_t sms_system_init(void) {
  ESP_LOGI(TAG, "🚀 init sms dispatcher...");

  // 1. Инициализация базового ассемблера SMS
  sms_assembler_config_t assembler_config = {.concat_timeout_ms = 60000,
                                             .auto_cleanup_enabled = true,
                                             .priority_queue_enabled = true,
                                             .default_priority = SMS_PRIORITY_NORMAL};

  esp_err_t ret = sms_assembler_init(&assembler_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "sms ass init error: %s", esp_err_to_name(ret));
    return ret;
  }

  // 2. Инициализация Telegram обработчика
  tg_bot_load_all_params();
  telegram_config_t telegram_config = {.bot_token = "",
                                       .chat_id = "",
                                       .user_id = "",
                                       .retry_enabled = true,
                                       .max_retries = 3,
                                       .retry_delay_ms = 5000,
                                       .format_markdown = true,
                                       .add_timestamps = false,
                                       .priority_notifications = true};

  strncpy(telegram_config.bot_token, tg_bot_get_token(), sizeof(telegram_config.bot_token) - 1);
  strncpy(telegram_config.chat_id, tg_bot_get_chat_id(), sizeof(telegram_config.chat_id) - 1);
  strncpy(telegram_config.user_id, tg_bot_get_user_id_sms(), sizeof(telegram_config.user_id));
  ret = sms_telegram_handler_init(&telegram_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Telegram init error: %s", esp_err_to_name(ret));
    return ret;
  }

  // 3. Регистрация обработчиков в ассемблере
  ret = sms_assembler_register_handler(SMS_HANDLER_TELEGRAM, sms_telegram_handler_process);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Telegram registred error: %s", esp_err_to_name(ret));
    return ret;
  }

  // // 4. Регистрация дополнительных обработчиков (опционально)
  // sms_assembler_register_handler(SMS_HANDLER_EMAIL, sms_email_handler_process);
  // sms_assembler_register_handler(SMS_HANDLER_WEBHOOK, sms_webhook_handler_process);
  // sms_assembler_register_handler(SMS_HANDLER_FILE, sms_file_handler_process);

  // // 5. Настройка приоритетов для конкретных отправителей
  // sms_assembler_set_sender_priority("+7900123456", SMS_PRIORITY_HIGH);    // Важный номер
  // sms_assembler_set_sender_priority("+7800000000", SMS_PRIORITY_URGENT);  // Критичный номер

  // 6. Настройка маски обработчиков для разных отправителей
  // bool telegram_only[SMS_HANDLER_MAX] = {true, false, false, false, false}; // Только Telegram
  // bool all_handlers[SMS_HANDLER_MAX] = {true, true, true, true, false};     // Все кроме custom
  // bool file_only[SMS_HANDLER_MAX] = {false, false, false, true, false};    // Только файл

  // sms_assembler_set_default_handlers("+7900123456", telegram_only);  // Важные SMS только в Telegram
  // sms_assembler_set_default_handlers("+7901000000", all_handlers);   // Обычные SMS везде
  // sms_assembler_set_default_handlers("+7902000000", file_only);      // Лог SMS только в файл

  // 7. Запуск диспетчера
  ret = sms_assembler_start_dispatcher(5); // Приоритет задачи 5
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "sms dispatcher start error: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "✅ sms dispatcher start OK");
  return ESP_OK;
}

/**
 * Тестовое сообщение телеграмм-боту
 */
esp_err_t teegram_send_test_handler(void) {
  sms_telegram_handler_test_send(NULL);
  return ESP_OK;
}

/**
 * Обработчик загрузки OTA файла
 */
static esp_err_t ota_file_upload_handler(const char *tag_name, const char *buf, const size_t size, const char *file_name) {
	static esp_ota_handle_t handle = 0;

  // Записываем данные в OTA раздел
  esp_err_t ret = ota_stream_write(&handle, buf, size);
  
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "❌ OTA write failed: %s", esp_err_to_name(ret));
    handle = 0;
  } else if (size > 0) {
    ESP_LOGD(TAG, "📝 OTA chunk written: %zu bytes", size);
  }
  
  return ret;
}

/**
 * Задача периодической проверки OTA обновлений
 */
static void ota_check_task(void *pvParameters) {
  const TickType_t check_interval = pdMS_TO_TICKS(OTA_CHECK_INTERVAL_HOURS * 60 * 60 * 1000);
  
  ESP_LOGD(TAG, "🔄 OTA check task started, interval: %d hours", OTA_CHECK_INTERVAL_HOURS);
  
  while (1) {
    // Ждем подключения к WiFi
    while (wifi_is_sta_connected() != ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(5000)); // Проверяем каждые 5 секунд
    }
    
    ESP_LOGD(TAG, "🔄 Starting OTA update check...");
    
    // Проверяем наличие настроек OTA сервера
    char ota_server_ip[16] = {0};
    esp_err_t ret = ota_server_adr_read(ota_server_ip);
    
    if (ret == ESP_OK && strlen(ota_server_ip) > 0) {
      ESP_LOGD(TAG, "📡 Checking updates on server: %s", ota_server_ip);
      
      // Запускаем проверку обновлений
      ota_check_on_server();
      
      ESP_LOGD(TAG, "✅ OTA check completed");
    } else {
      ESP_LOGW(TAG, "⚠️ OTA server not configured, skipping update check");
    }
    
    // Ждем до следующей проверки
    vTaskDelay(check_interval);
  }
}

/**
 * Запуск задачи периодической проверки OTA
 */
static esp_err_t start_ota_check_task(void) {
  if (ota_task_handle != NULL) {
    ESP_LOGW(TAG, "OTA check task already running");
    return ESP_OK;
  }
  
  BaseType_t result = xTaskCreatePinnedToCore(
    ota_check_task,           // Функция задачи
    "ota_check",              // Имя задачи
    OTA_TASK_STACK_SIZE,      // Размер стека
    NULL,                     // Параметры
    OTA_TASK_PRIORITY,        // Приоритет (самый низкий)
    &ota_task_handle,         // Хендл задачи
    PRO_CPU_NUM               // Ядро (противоположное WiFi)
  );
  
  if (result == pdPASS) {
    ESP_LOGD(TAG, "✅ OTA check task created successfully");
    return ESP_OK;
  } else {
    ESP_LOGE(TAG, "❌ Failed to create OTA check task");
    return ESP_FAIL;
  }
}

/**
 * Остановка задачи периодической проверки OTA
 */
static void stop_ota_check_task(void) {
  if (ota_task_handle != NULL) {
    vTaskDelete(ota_task_handle);
    ota_task_handle = NULL;
    ESP_LOGD(TAG, "🛑 OTA check task stopped");
  }
}

/**
 * Net обработчики
 */
void wifi_ip_disconnected_handler(void) { 
  stop_ota_check_task();
  web_server_stop(); 
}

void wifi_ip_connected_handler(wifi_mode_t mode, esp_ip4_addr_t ip) {
  static esp_ip4_addr_t ip_current = {.addr = 0};
  
  if (mode == WIFI_MODE_STA) {
    // Запускаем задачу OTA при подключении в режиме Station
    start_ota_check_task();
  }
  
  if (ip_current.addr != ip.addr) {
    ESP_LOGV(TAG, "got new IP, web server restart");
    ip_current = ip;
    web_server_stop();
  }
  web_server_start(ip);
}

static void network_status_callback(bool registered) {
  static bool status = false;
  if (status != registered) {
    ESP_LOGI(TAG, "change network status to %d", registered);
    status = registered;
  }
  if (registered) {
    sim900d_network_monitor_start(60 * 1000);
    ussd_cmd_start_balance_check(10000);
  }
}

/**
 * Параметры системы
 * version on html page
 */
esp_err_t read_version_param(const paramName_t paramName, char *value, size_t maxLen) {
  sprintf(value, "%s", version_app());
  return ESP_OK;
}

/**
 * Чтение IP адреса OTA сервера для отображения на веб-странице
 */
esp_err_t read_ota_ip_param(const paramName_t paramName, char *value, size_t maxLen) {
  esp_err_t ret = ota_server_adr_read(value);
  if (ret != ESP_OK) {
    memset(value, 0, maxLen);
  }
  return ESP_OK;
}

/**
 * Сохранение IP адреса OTA сервера из веб-формы
 */
esp_err_t write_ota_ip_param(const paramName_t paramName, const char *value, size_t maxLen) {
  if (value && strlen(value) > 0) {
    esp_err_t ret = ota_server_adr_save((char*)value);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "✅ OTA server IP saved: %s", value);
    } else {
      ESP_LOGE(TAG, "❌ Failed to save OTA server IP: %s", esp_err_to_name(ret));
    }
    return ret;
  }
  return ESP_OK;
}

/**
 * Задача мониторинга системы
 */
void sms_system_monitor_task(void *pvParameters) {
  const int monitor_interval_ms = 30000; // 30 секунд

  ESP_LOGI(TAG, "📊 Запуск мониторинга SMS системы");

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(monitor_interval_ms));

    ESP_LOGI(TAG, "📊 === МОНИТОРИНГ SMS СИСТЕМЫ ===");

    // Статистика ассемблера
    sms_assembler_print_stats();

    ESP_LOGI(TAG, "");

    // Статистика Telegram обработчика
    sms_telegram_handler_print_stats();

    // Проверяем свободное место в очереди
    int free_space = sms_assembler_get_queue_free_space();
    if (free_space < 5) {
      ESP_LOGW(TAG, "⚠️ Очередь почти заполнена! Свободно: %d слотов", free_space);
    }

    // Принудительная очистка устаревших SMS
    int cleaned = sms_assembler_cleanup_expired();
    if (cleaned > 0) {
      ESP_LOGW(TAG, "🗑️ Очищено устаревших SMS групп: %d", cleaned);
    }

    ESP_LOGI(TAG, "📊 === КОНЕЦ МОНИТОРИНГА ===");
  }
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
  esp_log_level_set(TAG, LOG_LOCAL_LEVEL);

  reboot_reason_check();
  console_start();

  // Можно вызывать много раз, главное вызвать
  esp_event_loop_create_default();

  if (!web_server_init()) {
    ESP_LOGE(TAG, "Failed init web server!");
    return;
  }

  paramReg(VERSION_PARAM, version_app_len() + 1, read_version_param, NULL, NULL);
  if (tg_bot_register_params() != ESP_OK) {
    ESP_LOGE(TAG, "tg param reg failed");
    return;
  }

  // Инициализация OTA клиента
  ota_client_cfg_t ota_cfg = {
    .task_priority = OTA_TASK_PRIORITY + 1,  // Приоритет для загрузки чуть выше чем у периодической проверки
    .xCoreID = PRO_CPU_NUM,                  // Ядро противоположное WiFi
    .ota_begin_func = NULL,                  // Не используем callback'и
    .ota_end_func = NULL
  };
  
  esp_err_t ota_init_ret = ota_init(&ota_cfg);
  if (ota_init_ret == ESP_OK) {
    ESP_LOGD(TAG, "✅ OTA client initialized successfully");
  } else {
    ESP_LOGW(TAG, "⚠️ OTA client init failed: %s", esp_err_to_name(ota_init_ret));
  }

  // Регистрация обработчика загрузки OTA файлов
  esp_err_t ota_reg_ret = web_reg_upload_file_func(OTA_FILE_PARAM, ota_file_upload_handler);
  if (ota_reg_ret == ESP_OK) {
    ESP_LOGD(TAG, "✅ OTA file upload handler registered");
  } else {
    ESP_LOGE(TAG, "❌ Failed to register OTA upload handler: %s", esp_err_to_name(ota_reg_ret));
  }

  wifi_init(wifi_ip_connected_handler, wifi_ip_disconnected_handler);

  wifi_mode_start_t wifi_mode = wifi_is_sta_param() == ESP_OK ? WIFI_START_STA : WIFI_START_AP;
  esp_err_t tmp = wifi_start(wifi_mode);
  if (tmp == ESP_OK) {
    ESP_LOGI(TAG, "WiFi started mode %s", wifi_mode == WIFI_START_AP ? "soft AP" : "Station");
  } else {
    ESP_LOGE(TAG, "Failed to start WiFi %s", esp_err_to_name(tmp));
    return;
  }

  tmp = sms_system_init();
  if (tmp != ESP_OK) {
    ESP_LOGE(TAG, "Failed init to sms dispatcher");
    return;
  }

  paramReg(TELERGAMM_TEST_CMD, 1, NULL, NULL, teegram_send_test_handler);

  static const uart_config_t sim900d_uart_cfg = {.baud_rate = 9600,
                                                 .data_bits = UART_DATA_8_BITS,
                                                 .parity = UART_PARITY_DISABLE,
                                                 .stop_bits = UART_STOP_BITS_1,
                                                 .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
  if (sim900d_uart_init(SIM900D_UART_NUM, &sim900d_uart_cfg, SIM900D_UART_TX, SIM900D_UART_RX, SIM900D_PWRKEY,
                        SIM900D_STATUS, SIM900D_RI) == ESP_OK) {
    ESP_LOGV(TAG, "SIM900D UART initialized");
    int reset_attempts = 3;
    bool reset_success = false;
    for (int i = 0; i < reset_attempts; ++i) {
      if (sim900d_reset(10000)) {
        reset_success = true;
        break;
      }
      ESP_LOGW(TAG, "SIM900D reset attempt %d failed", i + 1);
    }
    if (reset_success) {
      int baud = sim900d_uart_autobaud(1000);
      if (baud > 0) {
        ESP_LOGV(TAG, "SIM900D UART baud=%d", baud);
        sim900d_sms_set_callback(sms_assembler_process_incoming);
        sim900d_network_status_set_callback(network_status_callback);
        sim900d_service_start(SIM900S_SMS_MODE_PDU);
        tmp = ussd_cmd_init();
        if (tmp != ESP_OK) {
          ESP_LOGE(TAG, "Failed to init USSD commands module %s", esp_err_to_name(tmp));
        }
      } else {
        ESP_LOGE(TAG, "Failed auto-baud SIM900D");
      }
    } else {
      ESP_LOGE(TAG, "Failed to reset SIM900D");
    }
  } else {
    ESP_LOGE(TAG, "Failed to initialize SIM900D UART");
  }

  // xTaskCreate(sms_system_monitor_task, "sms_monitor", 4096, NULL, 3, NULL);
}
