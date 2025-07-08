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

#include "sim900d_command.h"
#include "sim900d_parser.h"
#include "sim900d_uart.h"

#define UART_BUF_SIZE 1024

#define SIM900D_NO_INDEX_MEM -1

typedef struct {
  uart_port_t uart_num;
  QueueHandle_t sms_queue;
  QueueHandle_t lowprio_cmd_queue; // Очередь низкоприоритетных команд
  sms_callback_t sms_cb;
  network_status_callback_t network_status_cb;
  TaskHandle_t uart_task_handle;
  TaskHandle_t sms_task_handle;
  int last_sms_index; // Запрошенный индекс СМС из памяти sim900
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

// Разрешение чтения из uart
#define SIM900D_UART_EVENT_READ_ENABLE (1 << 0)
// Флаг успешной регистрации в сети
#define SIM900D_UART_EVENT_NET_REGISTERED (1 << 1)
// Флаг, указывающий, что драйвер занят обработкой ответа
#define SIM900D_UART_EVENT_BUSY (1 << 2)

// Очередь низкоприоритетных команд

#define SIM900D_LOWPRIO_CMD_QUEUE_LEN 8
#define SIM900D_LOWPRIO_CMD_MAX_LEN 128

// Структура для передачи низкоприоритетной команды
typedef struct {
  char cmd[SIM900D_LOWPRIO_CMD_MAX_LEN];
  TickType_t timeout;
} sim900d_lowprio_cmd_t;

// Отправка AT-команды и ожидание ответа с логированием
static int sim900d_send_at(const char *cmd, char *response, size_t resp_size, TickType_t timeout) {
  // Логируем отправляемую команду
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

static bool sim900d_int_array_contains(const int *arr, size_t len, int value) {
  if (!arr || len == 0)
    return false;
  for (size_t i = 0; i < len; ++i) {
    if (arr[i] == value)
      return true;
  }
  return false;
}

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
static bool sim900d_enqueue_lowprio_cmd(const char *cmd, TickType_t timeout) {
  if (!handle.lowprio_cmd_queue || !cmd)
    return false;
  sim900d_lowprio_cmd_t msg = {0};
  strncpy(msg.cmd, cmd, SIM900D_LOWPRIO_CMD_MAX_LEN - 1);
  msg.timeout = timeout;
  return xQueueSend(handle.lowprio_cmd_queue, &msg, 0) == pdTRUE;
}

/****************************
 * HANDLERS
 **************************** */
/**
 * @brief Обработчик параметров состояния памяти SMS (+CPMS).
 * Если память заполнена, очищает её.
 */
static void sim900d_cpms_handler(Sim900dParsedParams *params) {
  if (!params || params->paramCount < 6) {
    ESP_LOGE(TAG, SIM900D_RESP_CPMS " invalid params");
    return;
  }
  // params: <mem1>,<used1>,<total1>,<mem2>,<used2>,<total2>
  int used1 = atoi(params->params[1]);
  int total1 = atoi(params->params[2]);
  int used2 = atoi(params->params[4]);
  int total2 = atoi(params->params[5]);
  ESP_LOGI(TAG, "CPMS: mem1=%s used1=%d total1=%d, mem2=%s used2=%d total2=%d", params->params[0], used1, total1,
           params->params[3], used2, total2);

  bool mem_full = (used1 >= total1) || (used2 >= total2);
  if (mem_full) {
    ESP_LOGW(TAG, "SMS memory full, deleting all messages...");
    sim900d_enqueue_lowprio_cmd(SIM900D_CMD_DELETE_ALL_SMS, 60000); // 1 минуту ждет
  }
  sim900d_send_at(SIM900D_CMD_CREG, NULL, 0, pdMS_TO_TICKS(500));
}

/**
 * @brief Обработчик параметров чтения SMS-сообщения (CMGR).
 */
static void sim900d_cmgr_handler(Sim900dParsedParams *params) {
  if (!params || params->paramCount < 4) {
    ESP_LOGE(TAG, SIM900D_RESP_CMGR " invalid params %d", params ? params->paramCount : 0);
    return;
  }
  for (int i = 0; i < params->paramCount; ++i) {
    ESP_LOGV(TAG, "CMGR param[%d]: %s", i, params->params[i]);
  }
  ESP_LOGV(TAG, "sms text %s", params->multilineBody);

  sms_message_t sms = {0};
  strncpy(sms.status, params->params[0], sizeof(sms.status) - 1);
  strncpy(sms.sender, params->params[1], sizeof(sms.sender) - 1);
  strncpy(sms.timestamp, params->params[3], sizeof(sms.timestamp) - 1);
  strncpy(sms.text, params->multilineBody, sizeof(sms.text) - 1);
  if (handle.last_sms_index != SIM900D_NO_INDEX_MEM) {
    sms.index = handle.last_sms_index;
  }
  if (handle.sms_queue) {
    xQueueSend(handle.sms_queue, &sms, 0);
    handle.last_sms_index = SIM900D_NO_INDEX_MEM;
  }
}

/**
 * @brief Обработчик параметров получения нового SMS (CMTI).
 */
static void sim900d_cmti_handler(Sim900dParsedParams *params) {
  if (!params || params->paramCount < 2) {
    ESP_LOGE(TAG, SIM900D_RESP_CMTI " invalid params");
    return;
  }
  const char *mem = params->params[0];
  int index = atoi(params->params[1]);
  ESP_LOGI(TAG, "New SMS indication: mem=%s, index=%d", mem, index);
  handle.last_sms_index = index;
  char cmd[32];
  snprintf(cmd, sizeof(cmd), SIM900D_CMD_READ_SMS_FMT, index);
  sim900d_send_at(cmd, NULL, 0, pdMS_TO_TICKS(SIM900D_UART_TX_WAIT_MS));
}

/**
 * @brief Обработчик параметров уведомлений о новых SMS (CNMI).
 */
static void sim900d_cnmi_test_handler(Sim900dParsedParams *params) {
  if (!params || params->paramCount < SIM900D_CNMI_PARAM_MAX) {
    ESP_LOGE(TAG, SIM900D_RESP_CNMI_TEST " invalid params");
    return;
  }
  ESP_LOGI(TAG, "CNMI response:");
  bool error = false;
  for (int i = 0; i < params->paramCount; ++i) {
    int count = 0;
    int *numbers = sim900d_parse_number_list(params->params[i], &count);
    if (numbers && count > 0) {
      switch (i) {
      case SIM900D_CNMI_PARAM_MODE:
        if (!sim900d_int_array_contains(numbers, count, SIM900D_CNMI_MODE_BUFFER_URC)) {
          ESP_LOGE(TAG, "    MODE not support %d", SIM900D_CNMI_MODE_BUFFER_URC);
          error = true;
        }
        break;
      case SIM900D_CNMI_PARAM_MT:
        if (!sim900d_int_array_contains(numbers, count, SIM900D_CNMI_MT_URC)) {
          ESP_LOGE(TAG, "    MT not support %d", SIM900D_CNMI_MT_URC);
          error = true;
        }
        break;
      case SIM900D_CNMI_PARAM_BM:
        if (!sim900d_int_array_contains(numbers, count, SIM900D_CNMI_BM_DISABLE)) {
          ESP_LOGE(TAG, "    BM not support %d", SIM900D_CNMI_BM_DISABLE);
          error = true;
        }
        break;
      case SIM900D_CNMI_PARAM_DS:
        if (!sim900d_int_array_contains(numbers, count, SIM900D_CNMI_DS_DISABLE)) {
          ESP_LOGE(TAG, "    DS not support %d", SIM900D_CNMI_DS_DISABLE);
          error = true;
        }
        break;
      case SIM900D_CNMI_PARAM_BFR:
        if (!sim900d_int_array_contains(numbers, count, SIM900D_CNMI_BFR_DISABLE)) {
          ESP_LOGE(TAG, "    BFR not support %d", SIM900D_CNMI_BFR_DISABLE);
          error = true;
        }
        break;
      default:
        break;
      }
    } else {
      ESP_LOGE(TAG, "    param[%d]: failed to parse or empty", i);
      error = true;
    }
    free(numbers);
  }
  if (!error) {
    sim900d_send_at(SIM900D_CMD_SMS_NOTIFY, NULL, 0, pdMS_TO_TICKS(500));
  } else {
    ESP_LOGE(TAG, "notifications will not be received!");
    // TODO: Добавить тут создание задачи периодической проверки смс
  }
}

/**
 * Обработчик параметров состояния PIN-кода SIM-карты (CPIN).
 */
static void sim900d_cpin_handler(Sim900dParsedParams *params) {
  if (!params || params->paramCount < 1) {
    ESP_LOGE(TAG, SIM900D_RESP_CPIN " invalid params");
    return;
  }
  const char *cpin_state = params->params[0];

  if (strcmp(cpin_state, SIM900D_RESP_READY) == 0) {
    ESP_LOGI(TAG, "SIM card is ready");
    sim900d_send_at(SIM900D_CMD_CPMS, NULL, 0, pdMS_TO_TICKS(500));
  } else if (strcmp(cpin_state, "SIM PIN") == 0) {
    ESP_LOGW(TAG, "SIM card requires PIN");
  } else if (strcmp(cpin_state, "SIM PUK") == 0) {
    ESP_LOGW(TAG, "SIM card requires PUK");
  } else {
    ESP_LOGW(TAG, "SIM card state: %s", cpin_state);
  }
}

/**
 * @brief Обработчик параметров регистрации в сети (CREG).
 */
static void sim900d_creg_handler(Sim900dParsedParams *params) {
  static bool network_state = false;
  if (!params || params->paramCount < 2) {
    ESP_LOGE(TAG, SIM900D_RESP_CREG " invalid params");
    return;
  }
  int n = atoi(params->params[0]);
  int stat = atoi(params->params[1]);
  ESP_LOGI(TAG, "CREG: n=%d, stat=%d", n, stat);
  switch (stat) {
  case 0:
    ESP_LOGW(TAG, "Not registered, not searching for operator");
    xEventGroupClearBits(handle.uart_event_group, SIM900D_UART_EVENT_NET_REGISTERED);
    break;
  case 1:
    ESP_LOGI(TAG, "Registered, home network");
    xEventGroupSetBits(handle.uart_event_group, SIM900D_UART_EVENT_NET_REGISTERED);
    sim900d_send_at(SIM900D_RESP_CNMI_TEST, NULL, 0, pdMS_TO_TICKS(500));
    break;
  case 2:
    ESP_LOGI(TAG, "Not registered, searching for operator");
    xEventGroupClearBits(handle.uart_event_group, SIM900D_UART_EVENT_NET_REGISTERED);
    // Продолжаем ожидание регистрации
    vTaskDelay(pdMS_TO_TICKS(1000));
    sim900d_send_at(SIM900D_CMD_CREG, NULL, 0, pdMS_TO_TICKS(500));
    break;
  case 3:
    ESP_LOGW(TAG, "Registration denied");
    xEventGroupClearBits(handle.uart_event_group, SIM900D_UART_EVENT_NET_REGISTERED);
    break;
  case 4:
    ESP_LOGW(TAG, "Unknown registration status");
    xEventGroupClearBits(handle.uart_event_group, SIM900D_UART_EVENT_NET_REGISTERED);
    break;
  case 5:
    ESP_LOGI(TAG, "Registered, roaming");
    xEventGroupSetBits(handle.uart_event_group, SIM900D_UART_EVENT_NET_REGISTERED);
    break;
  default:
    ESP_LOGW(TAG, "Unknown stat value: %d", stat);
    xEventGroupClearBits(handle.uart_event_group, SIM900D_UART_EVENT_NET_REGISTERED);
    break;
  }
  bool net_registered = (xEventGroupGetBits(handle.uart_event_group) & SIM900D_UART_EVENT_NET_REGISTERED) != 0;
  if (net_registered != network_state) {
    network_state = net_registered;
    if (handle.network_status_cb) {
      handle.network_status_cb(network_state);
    }
  }
}

static void sim900d_register_hndlers() {
  bool ok = true;
  ok &= sim900d_register_handler(SIM900D_RESP_CPIN, sim900d_cpin_handler) == ESP_OK;
  ok &= sim900d_register_handler(SIM900D_RESP_CREG, sim900d_creg_handler) == ESP_OK;
  ok &= sim900d_register_handler(SIM900D_RESP_SMS_NOTIFY, sim900d_cnmi_test_handler) == ESP_OK;
  ok &= sim900d_register_handler(SIM900D_RESP_CPMS, sim900d_cpms_handler) == ESP_OK;
  ok &= sim900d_register_handler(SIM900D_RESP_CMGR, sim900d_cmgr_handler) == ESP_OK;
  ok &= sim900d_register_handler(SIM900D_RESP_CMTI, sim900d_cmti_handler) == ESP_OK;

  if (!ok) {
    ESP_LOGE(TAG, "Failed to register SIM900D response handlers");
  }
}

/****************************
 * END HANDLERS
 **************************** */

void sim900d_service_start() {
  // Зпускается последовательность вызовов обработчиков
  sim900d_send_at(SIM900D_CMD_CPIN, NULL, 0, pdMS_TO_TICKS(500));
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

void sim900d_network_status_set_callback(network_status_callback_t cb) { handle.network_status_cb = cb; }

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
  handle.last_sms_index = SIM900D_NO_INDEX_MEM;
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
  handle.last_sms_index = SIM900D_NO_INDEX_MEM;

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
  sim900d_register_hndlers();

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