#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"

static const char *TAG = "SIM900";

#include "include/sim900d_uart.h"
#include "sim900d_command.h"
#include "sim900d_handlers.h"
#include "sim900d_parser.h"
#include "sim900d_uart_internal.h"

#define UART_BUF_SIZE 1024

typedef struct {
  uart_port_t uart_num;
  QueueHandle_t sms_queue;
  QueueHandle_t lowprio_cmd_queue; // Очередь низкоприоритетных команд
  sms_callback_t sms_cb;
  network_status_callback_t network_status_cb;
  TaskHandle_t uart_task_handle;
  TaskHandle_t sms_task_handle;
  TaskHandle_t network_task_handle;
  TaskHandle_t lowprio_cmd_task; // Обработка очереди низкоприоритетных команд
  gpio_num_t pwrkey_gpio;
  gpio_num_t status_gpio;
  gpio_num_t ri_gpio;                  // Может быть неопределен
  EventGroupHandle_t uart_event_group; // Состояние драйвера
} sim900d_uart_handle_t;

static sim900d_uart_handle_t handle = {.uart_num = UART_NUM_MAX};

// Список поддерживаемых скоростей UART для SIM900D
static const int baud_list[] = {115200, 57600, 38400, 19200, 9600, 4800, 2400, 1200};
#define SIM900D_UART_TX_WAIT_MS 1000

// Очередь низкоприоритетных команд
#define SIM900D_LOWPRIO_CMD_QUEUE_LEN 8
#define SIM900D_LOWPRIO_CMD_MAX_LEN 128

// Структура для передачи низкоприоритетной команды
typedef struct {
  char cmd[SIM900D_LOWPRIO_CMD_MAX_LEN];
  TickType_t timeout;
} sim900d_lowprio_cmd_t;

/******************************************************
 * Низкоуровневые функции приема/передачи на sim900d
 ****************************************************** */

// Только для использования внтури драйвера!
int sim900d_send_at(const char *cmd, char *response, size_t resp_size, TickType_t timeout) {
  ESP_LOGV(TAG, "UART->SIM900D: %s", cmd);
  // Драйвер занят
  xEventGroupClearBits(handle.uart_event_group, SIM900D_UART_EVENT_BUSY);
  uart_write_bytes(handle.uart_num, cmd, strlen(cmd));
  uart_write_bytes(handle.uart_num, "\r\n", 2);

  if (response && (resp_size > 1)) {
    int len = uart_read_bytes(handle.uart_num, (uint8_t *)response, resp_size - 1, timeout);
    if (len > 0) {
      response[len] = 0;
      // Логируем полученный ответ
      ESP_LOGV(TAG, "SIM900D->UART: %s", response);
      return len;
    }

    response[0] = 0;
    ESP_LOGV(TAG, "SIM900D->UART: <no response>");
  }

  return 0;
}

static void sim900d_uart_read_task(void *pvParameters) {
  uint8_t buf[UART_BUF_SIZE + 1];
  while (1) {
    ESP_LOGV(TAG, "Bloced?");
    // Ждём, пока установлен бит разрешения чтения
    xEventGroupWaitBits(handle.uart_event_group, SIM900D_UART_EVENT_READ_ENABLE, pdFALSE, pdTRUE, portMAX_DELAY);
    while (xEventGroupGetBits(handle.uart_event_group) & SIM900D_UART_EVENT_READ_ENABLE) {
      int len = uart_read_bytes(handle.uart_num, buf, UART_BUF_SIZE, pdMS_TO_TICKS(100));
      if (len > 0) {
        buf[len] = 0;
        ESP_LOGV(TAG, "SIM900D->UART:%s", buf);
        if (sim900d_parse_line((char *)buf) == PARSE_STATE_IN_PROGRESS) {
          xEventGroupSetBits(handle.uart_event_group, SIM900D_UART_EVENT_BUSY);
        } else {
          xEventGroupClearBits(handle.uart_event_group, SIM900D_UART_EVENT_BUSY);
        }
        ESP_LOGV(TAG, "Busy:%d", xEventGroupGetBits(handle.uart_event_group) & SIM900D_UART_EVENT_BUSY ? 1 : 0);
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    // Если бит снят, снова ждём разрешения
  }
}

/******************************************************
 * Служебные функции
 ****************************************************** */

bool sim900d_enqueue_lowprio_cmd(const char *cmd, TickType_t timeout) {
  if (!handle.lowprio_cmd_queue || !cmd)
    return false;
  sim900d_lowprio_cmd_t msg = {0};
  strncpy(msg.cmd, cmd, SIM900D_LOWPRIO_CMD_MAX_LEN - 1);
  msg.timeout = timeout;
  return xQueueSend(handle.lowprio_cmd_queue, &msg, 0) == pdTRUE;
}

EventGroupHandle_t sim900d_get_event_group(void) { return handle.uart_event_group; }

void sim900d_service_start() {
  char format_cmd[32];
  sim900d_sms_mode_t sms_format = SMS_MODE_PDU;
  snprintf(format_cmd, sizeof(format_cmd), SIM900D_CMD_CMGF_MODE, sms_format == SMS_MODE_PDU ? 0 : 1);
  char response_buffer[64];
  int len = sim900d_send_at(format_cmd, response_buffer, sizeof(response_buffer), pdMS_TO_TICKS(500));
  if (len >= 0 && strstr(response_buffer, "OK") != NULL) {
    // Зпускается последовательность вызовов обработчиков
    ESP_LOGI(TAG, "SIM900D->UART:SMS mode set to %s", sms_format == SMS_MODE_PDU ? "PDU" : "TEXT");
    sim900d_send_at(SIM900D_CMD_CPIN, NULL, 0, pdMS_TO_TICKS(500));
  } else {
    ESP_LOGE(TAG, "set sms mode fail %s", response_buffer);
  }
}

int sim900d_uart_autobaud(uint32_t timeout_ms) {
  if (timeout_ms == 0)
    timeout_ms = 1000;

  size_t baud_count = sizeof(baud_list) / sizeof(baud_list[0]);
  uart_port_t uart_num = handle.uart_num;

  // Снимаем бит разрешения чтения UART (запретить чтение) и драйвер занят
  if (handle.uart_event_group) {
    xEventGroupClearBits(handle.uart_event_group, SIM900D_UART_EVENT_READ_ENABLE);
    xEventGroupSetBits(handle.uart_event_group, SIM900D_UART_EVENT_BUSY);
  }

  int result = -1;
  for (size_t i = 0; i < baud_count; ++i) {
    uart_set_baudrate(uart_num, baud_list[i]);
    uart_flush(uart_num);

    char resp[64];
    int len = sim900d_send_at(SIM900D_CMD_AT, resp, sizeof(resp), pdMS_TO_TICKS(timeout_ms));
    if (len > 0 && strstr(resp, SIM900D_CMD_AT)) {
      ESP_LOGI(TAG, "SIM900D autobaud success: %d", baud_list[i]);
      result = baud_list[i];
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // Устанавливаем бит разрешения чтения UART (разрешить чтение) и дравйер свободен
  if (handle.uart_event_group) {
    xEventGroupSetBits(handle.uart_event_group, SIM900D_UART_EVENT_READ_ENABLE);
    xEventGroupClearBits(handle.uart_event_group, SIM900D_UART_EVENT_BUSY);
  }

  if (result < 0) {
    ESP_LOGE(TAG, "SIM900D autobaud failed");
  }
  return result;
}

bool sim900d_reset(uint32_t timeout_ms) {
  gpio_num_t status_gpio = handle.status_gpio;
  gpio_num_t pwrkey_gpio = handle.pwrkey_gpio;
  if ((status_gpio == GPIO_NUM_NC) || (pwrkey_gpio == GPIO_NUM_NC)) {
    return false;
  }

  // --- Шаг 1: выключение, если включен ---
  if (gpio_get_level(status_gpio) == 1) {
    ESP_LOGV(TAG, "SIM900D is ON, turning OFF...");
    gpio_set_level(pwrkey_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(pwrkey_gpio, 1);

    uint32_t waited = 0;
    while (gpio_get_level(status_gpio) == 1 && waited < timeout_ms) {
      vTaskDelay(pdMS_TO_TICKS(100));
      waited += 100;
    }

    if (gpio_get_level(status_gpio) == 1) {
      ESP_LOGE(TAG, "Failed to turn off SIM900D");
      return false;
    }
    ESP_LOGV(TAG, "SIM900D powered off");
    vTaskDelay(pdMS_TO_TICKS(3000));
  }

  // --- Шаг 2: включение ---
  ESP_LOGV(TAG, "Turning SIM900D ON...");
  gpio_set_level(pwrkey_gpio, 0);
  vTaskDelay(pdMS_TO_TICKS(1500));
  gpio_set_level(pwrkey_gpio, 1);

  uint32_t waited = 0;
  while (gpio_get_level(status_gpio) == 0 && waited < (timeout_ms + 3000)) {
    vTaskDelay(pdMS_TO_TICKS(100));
    waited += 100;
  }

  if (gpio_get_level(status_gpio) == 1) {
    ESP_LOGV(TAG, "SIM900D powered on");
    return true;
  } else {
    ESP_LOGE(TAG, "Failed to power on SIM900D");
    return false;
  }
}

/**
 * @brief Задача обработки низкоприоритетных команд для SIM900D.
 *
 * Эта задача ожидает поступления команд в очередь sim900d_lowprio_cmd_queue.
 * После получения команды задача проверяет, не занят ли драйвер UART (ожидает освобождения, если занят).
 * Затем отправляет AT-команду модулю SIM900D с помощью функции sim900d_send_at.
 *
 * @param pvParameters Не используется (может быть NULL).
 */
static void sim900d_lowprio_cmd_task(void *pvParameters) {
  sim900d_lowprio_cmd_t cmd_msg;
  while (1) {
    if (xQueueReceive(handle.lowprio_cmd_queue, &cmd_msg, portMAX_DELAY) == pdTRUE) {
      // Ждём, пока драйвер не занят
      while (xEventGroupGetBits(handle.uart_event_group) & SIM900D_UART_EVENT_BUSY) {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      sim900d_send_at(cmd_msg.cmd, NULL, 0, cmd_msg.timeout);
    }
  }
}

/******************************************************
 * Функции приема/передачи сообщений
 ****************************************************** */
static void sim900d_sms_task(void *pvParameters) {
  sms_message_t sms;

  while (1) {
    if (xQueueReceive(handle.sms_queue, &sms, portMAX_DELAY) == pdTRUE) {
      if (handle.sms_queue) {
        // СМС удачно обработана и она была в памяти
        if ((handle.sms_cb(&sms) == ESP_OK) && (sms.index != SIM900D_NO_INDEX_MEM)) {
          char del_cmd[32];
          snprintf(del_cmd, sizeof(del_cmd), SIM900D_CMD_DELETE_SMS_BY_INDEX_FMT, sms.index);
          sim900d_enqueue_lowprio_cmd(del_cmd, pdMS_TO_TICKS(500));
        }
      }
    }
  }
}

void sim900d_sms_set_callback(sms_callback_t cb) { handle.sms_cb = cb; }

// Только для использования внтури драйвера!
void sim900d_enqueue_sms(const sms_message_t *sms) {
  if (handle.sms_queue && sms)
    xQueueSend(handle.sms_queue, sms, 0);
}

/******************************************************
 * Функции мониторинга состояния сети
 ****************************************************** */

void sim900d_network_status_set_callback(network_status_callback_t cb) { handle.network_status_cb = cb; }

void sim900d_notify_network_status(bool registered) {
  static bool last_state = false;
  if (last_state != registered) {
    last_state = registered;
    if (handle.network_status_cb) {
      handle.network_status_cb(registered);
    }
  }
}

static void sim900d_network_monitor_task(void *pvParameters) {
  uint32_t period_ms = *((uint32_t *)pvParameters);
  while (1) {
    sim900d_enqueue_lowprio_cmd(SIM900D_CMD_CREG, 500);
    vTaskDelay(pdMS_TO_TICKS(period_ms));
  }
  vTaskDelete(NULL);
}

/**
 * @brief Остановить задачу мониторинга сети.
 */
static void sim900d_network_monitor_stop(void) {
  if (handle.network_task_handle) {
    // Дадим задаче время завершиться
    vTaskDelay(pdMS_TO_TICKS(100));
    handle.network_task_handle = NULL;
  }
}

/**
 * @brief Запустить задачу мониторинга сети.
 * @param period_ms Период опроса в миллисекундах.
 */
void sim900d_network_monitor_start(uint32_t period_ms) {
  sim900d_network_monitor_stop();
  uint32_t *param = malloc(sizeof(uint32_t));
  if (!param)
    return;
  *param = period_ms;
  if (xTaskCreate(sim900d_network_monitor_task, "sim900d_network_monitor_task", 2048, param, 5,
                  &handle.network_task_handle) != pdPASS) {
    free(param);
  }
}

/******************************************************
 * Инициализация драйвера
 ****************************************************** */
static void sim900d_uart_cleanup(uart_port_t uart_num) {
  sim900d_network_monitor_stop();
  if (handle.sms_queue) {
    vQueueDelete(handle.sms_queue);
    handle.sms_queue = NULL;
  }
  if (handle.lowprio_cmd_queue) {
    vQueueDelete(handle.lowprio_cmd_queue);
    handle.lowprio_cmd_queue = NULL;
  }
  if (handle.lowprio_cmd_queue) {
    vQueueDelete(handle.lowprio_cmd_queue);
    handle.lowprio_cmd_queue = NULL;
  }
  if (handle.uart_event_group) {
    vEventGroupDelete(handle.uart_event_group);
    handle.uart_event_group = NULL;
  }
  if (handle.uart_task_handle)
    vTaskDelete(handle.uart_task_handle);
  if (handle.sms_task_handle)
    vTaskDelete(handle.sms_task_handle);
  if (handle.network_task_handle)
    vTaskDelete(handle.network_task_handle);
  handle.uart_task_handle = NULL;
  handle.sms_task_handle = NULL;
  handle.network_task_handle = NULL;
  handle.sms_cb = NULL;
  handle.network_status_cb = NULL;
  if (handle.uart_num != UART_NUM_MAX) {
    uart_driver_delete(uart_num);
    handle.uart_num = UART_NUM_MAX;
  }
}

esp_err_t sim900d_uart_init(uart_port_t uart_num, const uart_config_t *uart_config, gpio_num_t txd_pin,
                            gpio_num_t rxd_pin, gpio_num_t pwrkey_pin, gpio_num_t status_pin, gpio_num_t ri_pin) {
  if (!uart_config || (txd_pin == GPIO_NUM_NC) || (rxd_pin == GPIO_NUM_NC) || (pwrkey_pin == GPIO_NUM_NC) ||
      (status_pin == GPIO_NUM_NC))
    return ESP_ERR_INVALID_ARG;

#if CONFIG_LOG_DEFAULT_LEVEL > 1
  esp_log_level_set(TAG, LOG_LOCAL_LEVEL);
#endif

  handle.uart_num = uart_num;
  handle.sms_queue = xQueueCreate(8, sizeof(sms_message_t));
  handle.lowprio_cmd_queue = xQueueCreate(SIM900D_LOWPRIO_CMD_QUEUE_LEN, sizeof(sim900d_lowprio_cmd_t));
  handle.sms_cb = NULL;
  handle.network_status_cb = NULL;
  handle.pwrkey_gpio = pwrkey_pin;
  handle.status_gpio = status_pin;
  handle.ri_gpio = ri_pin;
  handle.uart_task_handle = NULL;
  handle.sms_task_handle = NULL;

  if ((!handle.sms_queue) || (!handle.lowprio_cmd_queue)) {
    sim900d_uart_cleanup(uart_num);
    return ESP_ERR_NO_MEM;
  }

  if (uart_driver_install(uart_num, UART_BUF_SIZE * 2, 0, 0, NULL, 0) != ESP_OK) {
    sim900d_uart_cleanup(uart_num);
    return ESP_FAIL;
  }
  if (uart_param_config(uart_num, uart_config) != ESP_OK) {
    sim900d_uart_cleanup(uart_num);
    return ESP_ERR_INVALID_ARG;
  }
  if (uart_set_pin(uart_num, txd_pin, rxd_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
    sim900d_uart_cleanup(uart_num);
    return ESP_ERR_INVALID_ARG;
  }

  gpio_set_direction(pwrkey_pin, GPIO_MODE_OUTPUT);
  gpio_set_pull_mode(pwrkey_pin, GPIO_FLOATING);

  gpio_set_direction(status_pin, GPIO_MODE_INPUT);
  gpio_set_pull_mode(status_pin, GPIO_FLOATING);

  if (sim900d_parser_init() != ESP_OK) {
    sim900d_uart_cleanup(uart_num);
    return ESP_ERR_NO_MEM;
  }
  sim900d_register_handlers();

  // Создаём группу событий для управления
  handle.uart_event_group = xEventGroupCreate();
  if (!handle.uart_event_group) {
    sim900d_uart_cleanup(uart_num);
    return ESP_ERR_NO_MEM;
  }
  // Разрешаем чтение по умолчанию
  xEventGroupSetBits(handle.uart_event_group, SIM900D_UART_EVENT_READ_ENABLE);
  // Нет регистрации в сети
  xEventGroupClearBits(handle.uart_event_group, SIM900D_UART_EVENT_NET_REGISTERED);
  // Драйвер свободен
  xEventGroupClearBits(handle.uart_event_group, SIM900D_UART_EVENT_BUSY);

  if (xTaskCreate(sim900d_uart_read_task, "sim900d_uart_read_task", 1024 * 6, &handle, 10, &handle.uart_task_handle) !=
      pdPASS) {
    sim900d_uart_cleanup(uart_num);
    return ESP_ERR_NO_MEM;
  }

  if (xTaskCreate(sim900d_sms_task, "sim900d_sms_task", 4096, &handle, 10, &handle.sms_task_handle) != pdPASS) {
    sim900d_uart_cleanup(uart_num);
    return ESP_ERR_NO_MEM;
  }

  if (!handle.lowprio_cmd_queue) {
    sim900d_uart_cleanup(uart_num);
    return ESP_ERR_NO_MEM;
  }
  if (xTaskCreate(sim900d_lowprio_cmd_task, "sim900d_lowprio_cmd_task", 2048 * 2, NULL, 1, &handle.lowprio_cmd_task) !=
      pdPASS) {
    sim900d_uart_cleanup(uart_num);
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

void sim900d_uart_deinit() { sim900d_uart_cleanup(handle.uart_num); }