#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"

static const char *TAG = "SIM900";

#include "sim900d_uart.h"

#define UART_BUF_SIZE 1024

/**
 * @brief Базовая AT-команда для инициализации связи с SIM900D.
 */
#define SIM900D_CMD_AT "AT\r\n"

/**
 * @brief Установка текстового режима SMS (AT+CMGF=1).
 */
#define SIM900D_CMD_SMS_MODE "AT+CMGF=1\r\n"

/**
 * @brief Включение уведомлений о новых SMS (AT+CNMI=2,1,0,0,0).
 */
#define SIM900D_CMD_SMS_NOTIFY "AT+CNMI=2,1,0,0,0\r\n"

/**
 * @brief Форматированная команда для чтения SMS по индексу (AT+CMGR=%d).
 */
#define SIM900D_CMD_READ_SMS_FMT "AT+CMGR=%d\r\n"

/**
 * @brief Запрос состояния PIN-кода SIM-карты (AT+CPIN?).
 */
#define SIM900D_CMD_CPIN "AT+CPIN?\r\n"

/**
 * @brief Запрос статуса регистрации в сети (AT+CREG?).
 */
#define SIM900D_CMD_CREG "AT+CREG?\r\n"

/**
 * @brief Запрос статуса регистрации в GPRS-сети (AT+CGREG?).
 */
#define SIM900D_CMD_CGREG "AT+CGREG?\r\n"

/**
 * @brief Запрос уровня сигнала (AT+CSQ).
 */
#define SIM900D_CMD_CSQ "AT+CSQ\r\n"

/**
 * @brief Запрос информации об операторе (AT+COPS?).
 */
#define SIM900D_CMD_COPS "AT+COPS?\r\n"

/**
 * @brief Ответ модуля об успешном выполнении команды ("OK").
 */
#define SIM900D_RESP_OK "OK"

/**
 * @brief Ответ модуля об ошибке ("ERROR").
 */
#define SIM900D_RESP_ERROR "ERROR"

/**
 * @brief Префикс уведомления о новом SMS ("+CMTI:").
 */
#define SIM900D_RESP_CMTI "+CMTI:"

/**
 * @brief Префикс ответа на команду чтения SMS ("+CMGR:").
 */
#define SIM900D_RESP_CMGR "+CMGR:"

/**
 * @brief Префикс ответа на запрос PIN-кода ("+CPIN:").
 */
#define SIM900D_RESP_CPIN "+CPIN:"

/**
 * @brief Префикс ответа на запрос регистрации в сети ("+CREG:").
 */
#define SIM900D_RESP_CREG "+CREG:"

/**
 * @brief Префикс ответа на запрос регистрации в GPRS ("+CGREG:").
 */
#define SIM900D_RESP_CGREG "+CGREG:"

/**
 * @brief Префикс ответа на запрос уровня сигнала ("+CSQ:").
 */
#define SIM900D_RESP_CSQ "+CSQ:"

/**
 * @brief Префикс ответа на запрос информации об операторе ("+COPS:").
 */
#define SIM900D_RESP_COPS "+COPS:"

typedef struct {
  uart_port_t uart_num;
  QueueHandle_t sms_queue;
  sms_callback_t sms_cb;
  TaskHandle_t uart_task_handle;
  TaskHandle_t sms_task_handle;
  gpio_num_t pwrkey_gpio;
  gpio_num_t status_gpio;
  gpio_num_t ri_gpio; // Может быть неопределен
} sim900d_uart_handle_t_internal;

struct sim900d_uart_handle_t {
  sim900d_uart_handle_t_internal internal;
};

// Список поддерживаемых скоростей UART для SIM900D
static const int baud_list[] = {115200, 57600, 38400, 19200, 9600, 4800, 2400, 1200};

static void sim900d_sms_task(void *pvParameters) {
  sim900d_uart_handle_t *handle = (sim900d_uart_handle_t *)pvParameters;
  sms_message_t sms;

  while (1) {
    if (xQueueReceive(handle->internal.sms_queue, &sms, portMAX_DELAY) == pdTRUE) {
      if (handle->internal.sms_queue) {
        handle->internal.sms_cb(&sms);
      }
    }
  }
}

// Отправка AT-команды и ожидание ответа с логированием
static int sim900d_send_at(sim900d_uart_handle_t *handle, const char *cmd, char *response, size_t resp_size,
                           TickType_t timeout) {
  // Логируем отправляемую команду
  ESP_LOGV(TAG, "UART->SIM900D: %s", cmd);

  uart_write_bytes(handle->internal.uart_num, cmd, strlen(cmd));
  uart_write_bytes(handle->internal.uart_num, "\r\n", 2);

  if (response) {
    int len = uart_read_bytes(handle->internal.uart_num, (uint8_t *)response, resp_size - 1, timeout);
    if (len > 0) {
      response[len] = 0;
      // Логируем полученный ответ
      ESP_LOGV(TAG, "SIM900D->UART: %s", response);
      return len;
    }
    response[0] = 0;
  }
  ESP_LOGV(TAG, "SIM900D->UART: <no response>");
  return 0;
}

// Парсинг +CMTI: "SM",index
static int parse_cmti(const char *buf) {
  const char *p = strstr(buf, SIM900D_RESP_CMTI);
  if (!p)
    return -1;
  int idx = -1;
  sscanf(p, "+CMTI: \"SM\",%d", &idx);
  return idx;
}

// Чтение SMS по индексу
static int sim900d_read_sms(sim900d_uart_handle_t *handle, int index, sms_message_t *sms) {
  char cmd[32], resp[256];
  snprintf(cmd, sizeof(cmd), SIM900D_CMD_READ_SMS_FMT, index);
  int len = sim900d_send_at(handle, cmd, resp, sizeof(resp), pdMS_TO_TICKS(2000));
  if (len <= 0)
    return -1;

  // Пример ответа:
  // +CMGR: "REC UNREAD","+79161234567","","23/05/25,12:34:56+12"
  // Hello world
  char *p = strstr(resp, SIM900D_RESP_CMGR);
  if (!p)
    return -1;
  char *num_start = strchr(p, '\"');
  if (!num_start)
    return -1;
  num_start = strchr(num_start + 1, '\"');
  if (!num_start)
    return -1;
  num_start++;
  char *num_end = strchr(num_start, '\"');
  if (!num_end)
    return -1;
  int num_len = num_end - num_start;
  strncpy(sms->sender, num_start, num_len);
  sms->sender[num_len] = 0;

  // Текст SMS после второй строки
  char *text = strchr(num_end, '\n');
  if (!text)
    return -1;
  text++; // skip '\n'
  strncpy(sms->text, text, sizeof(sms->text) - 1);
  sms->text[sizeof(sms->text) - 1] = 0;
  sms->index = index;
  return 0;
}

static void uart_task(void *pvParameters) {
  sim900d_uart_handle_t *handle = (sim900d_uart_handle_t *)pvParameters;

  if (sim900d_reset(handle, 10000)) {
    ESP_LOGV(TAG, "SIM900D reset OK");
  } else {
    vTaskDelete(NULL);
    ESP_LOGE(TAG, "SIM900D reset error");
  }

  // Настройка SIM900D для уведомлений о новых SMS
  sim900d_send_at(handle, SIM900D_CMD_SMS_NOTIFY, NULL, 0, pdMS_TO_TICKS(1000));
  sim900d_send_at(handle, SIM900D_CMD_SMS_MODE, NULL, 0, pdMS_TO_TICKS(1000)); // Текстовый режим SMS

  char buf[UART_BUF_SIZE];
  while (1) {
    int len = uart_read_bytes(handle->internal.uart_num, (uint8_t *)buf, sizeof(buf) - 1, pdMS_TO_TICKS(1000));
    if (len > 0) {
      buf[len] = 0;
      int sms_idx = parse_cmti(buf);
      if (sms_idx > 0) {
        sms_message_t sms;
        if (sim900d_read_sms(handle, sms_idx, &sms) == 0) {
          if (handle->internal.sms_queue) {
            xQueueSend(handle->internal.sms_queue, &sms, 0);
          }
          if (handle->internal.sms_cb) {
            handle->internal.sms_cb(&sms);
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// FSM: ожидание строки по шаблону
static bool sim900d_wait_for_response(sim900d_uart_handle_t *handle, const char *expected, char *out_buf,
                                      size_t out_buf_len, int timeout_ms) {
  char line[256] = {0};
  int offset = 0;
  int waited = 0;

  while (waited < timeout_ms) {
    int len = uart_read_bytes(handle->internal.uart_num, (uint8_t *)line + offset, 1, pdMS_TO_TICKS(100));
    if (len == 1) {
      if (line[offset] == '\n') {
        line[offset + 1] = '\0';
        char *clean_line = line;
        while (*clean_line == '\r' || *clean_line == '\n')
          clean_line++;
        if (strstr(clean_line, expected)) {
          if (out_buf && out_buf_len > 0) {
            strncpy(out_buf, clean_line, out_buf_len - 1);
            out_buf[out_buf_len - 1] = '\0';
          }
          return true;
        }
        offset = 0;
        memset(line, 0, sizeof(line));
      } else if (offset < sizeof(line) - 2) {
        offset++;
      } else {
        offset = 0;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    waited += 10;
  }
  return false;
}

// FSM: старт проверки сети
void sim900d_network_start(sim900d_uart_handle_t *handle, int attmpt_count) {
  typedef enum {
    STATE_AT,
    STATE_CPIN,
    STATE_CREG,
    STATE_CGREG,
    STATE_CSQ,
    STATE_COPS,
    STATE_DONE,
    STATE_ERROR
  } fsm_state_t;

  fsm_state_t state = STATE_AT;
  int retry_count = 0;
  char resp[256] = {0};
  int creg = -1, cgreg = -1, csq = -1;
  char operator_name[32] = {0};

  ESP_LOGI(TAG, "Starting SIM900D network FSM");

  while (state != STATE_DONE && state != STATE_ERROR) {
    switch (state) {
    case STATE_AT:
      sim900d_send_at(handle, SIM900D_CMD_AT, NULL, 0, pdMS_TO_TICKS(500));
      if (sim900d_wait_for_response(handle, SIM900D_RESP_OK, NULL, 0, 2000)) {
        state = STATE_CPIN;
      } else {
        state = STATE_ERROR;
      }
      break;

    case STATE_CPIN:
      sim900d_send_at(handle, SIM900D_CMD_CPIN, NULL, 0, pdMS_TO_TICKS(500));
      if (sim900d_wait_for_response(handle, SIM900D_RESP_CPIN, resp, sizeof(resp), 2000)) {
        if (strstr(resp, "READY")) {
          state = STATE_CREG;
        } else {
          ESP_LOGW(TAG, "SIM not ready: %s", resp);
          state = STATE_ERROR;
        }
      } else {
        state = STATE_ERROR;
      }
      break;

    case STATE_CREG:
      sim900d_send_at(handle, SIM900D_CMD_CREG, NULL, 0, pdMS_TO_TICKS(500));
      if (sim900d_wait_for_response(handle, SIM900D_RESP_CREG, resp, sizeof(resp), 2000)) {
        if (sscanf(resp, "+CREG: 0,%d", &creg) == 1 && (creg == 1 || creg == 5)) {
          state = STATE_CGREG;
        } else {
          ESP_LOGW(TAG, "CREG not registered: %s", resp);
          state = STATE_ERROR;
        }
      } else {
        state = STATE_ERROR;
      }
      break;

    case STATE_CGREG:
      sim900d_send_at(handle, SIM900D_CMD_CGREG, NULL, 0, pdMS_TO_TICKS(500));
      if (sim900d_wait_for_response(handle, SIM900D_RESP_CGREG, resp, sizeof(resp), 2000)) {
        if (sscanf(resp, "+CGREG: 0,%d", &cgreg) == 1 && (cgreg == 1 || cgreg == 5)) {
          state = STATE_CSQ;
        } else {
          ESP_LOGW(TAG, "CGREG not registered: %s", resp);
          state = STATE_ERROR;
        }
      } else {
        state = STATE_ERROR;
      }
      break;

    case STATE_CSQ:
      sim900d_send_at(handle, SIM900D_CMD_CSQ, NULL, 0, pdMS_TO_TICKS(500));
      if (sim900d_wait_for_response(handle, SIM900D_RESP_CSQ, resp, sizeof(resp), 2000)) {
        if (sscanf(resp, "+CSQ: %d", &csq) == 1) {
          int dBm = -113 + 2 * csq;
          ESP_LOGI(TAG, "Signal strength: %d (%d dBm)", csq, dBm);
          state = STATE_COPS;
        } else {
          state = STATE_ERROR;
        }
      } else {
        state = STATE_ERROR;
      }
      break;

    case STATE_COPS:
      sim900d_send_at(handle, SIM900D_CMD_COPS, NULL, 0, pdMS_TO_TICKS(500));
      if (sim900d_wait_for_response(handle, SIM900D_RESP_COPS, resp, sizeof(resp), 2000)) {
        if (sscanf(resp, "+COPS: 0,0,\"%31[^\"]\"", operator_name) == 1) {
          ESP_LOGI(TAG, "Operator: %s", operator_name);
          state = STATE_DONE;
        } else {
          state = STATE_ERROR;
        }
      } else {
        state = STATE_ERROR;
      }
      break;

    default:
      state = STATE_ERROR;
      break;
    }

    if (state == STATE_ERROR) {
      if (++retry_count < attmpt_count) {
        ESP_LOGW(TAG, "FSM retry %d/3", retry_count);
        state = STATE_AT;
        vTaskDelay(pdMS_TO_TICKS(1000));
      } else {
        ESP_LOGE(TAG, "FSM failed");
        break;
      }
    }
  }

  if (state == STATE_DONE) {
    ESP_LOGI(TAG, "FSM finished successfully");
  }
}

int sim900d_uart_autobaud(sim900d_uart_handle_t *handle, uint32_t timeout_ms) {
  if (!handle)
    return -1;
  if (timeout_ms == 0)
    timeout_ms = 1000;

  size_t baud_count = sizeof(baud_list) / sizeof(baud_list[0]);

  uart_port_t uart_num = handle->internal.uart_num;
  for (size_t i = 0; i < baud_count; ++i) {
    uart_set_baudrate(uart_num, baud_list[i]);
    uart_flush(uart_num);

    // Отправляем AT и ждём OK
    char resp[64];
    int len = sim900d_send_at(handle, SIM900D_CMD_AT, resp, sizeof(resp), pdMS_TO_TICKS(timeout_ms));
    if (len > 0 && strstr(resp, SIM900D_CMD_AT)) {
      ESP_LOGI(TAG, "SIM900D autobaud success: %d", baud_list[i]);
      return baud_list[i];
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  ESP_LOGE(TAG, "SIM900D autobaud failed");
  return -1;
}

bool sim900d_check_alive(sim900d_uart_handle_t *handle, const uint32_t timeout_ms) {
  if (!handle)
    return false;
  return sim900d_wait_for_response(handle, SIM900D_RESP_OK, NULL, 0, 2000);
}

bool sim900d_reset(sim900d_uart_handle_t *handle, uint32_t timeout_ms) {
  if (!handle)
    return false;

  gpio_num_t status_gpio = handle->internal.status_gpio;
  gpio_num_t pwrkey_gpio = handle->internal.pwrkey_gpio;
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
  while (gpio_get_level(status_gpio) == 0 && waited < (timeout_ms+3000)) {
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

esp_err_t sim900d_uart_init(sim900d_uart_handle_t **out_handle, uart_port_t uart_num, const uart_config_t *uart_config,
                            gpio_num_t txd_pin, gpio_num_t rxd_pin, gpio_num_t pwrkey_pin, gpio_num_t status_pin,
                            gpio_num_t ri_pin) {
  if (!out_handle || !uart_config || (txd_pin == GPIO_NUM_NC) || (rxd_pin == GPIO_NUM_NC) ||
      (pwrkey_pin == GPIO_NUM_NC) || (status_pin == GPIO_NUM_NC))
    return ESP_ERR_INVALID_ARG;

#if CONFIG_LOG_DEFAULT_LEVEL > 1
  esp_log_level_set(TAG, LOG_LOCAL_LEVEL);
#endif

  sim900d_uart_handle_t *handle = calloc(1, sizeof(sim900d_uart_handle_t));
  if (!handle)
    return ESP_ERR_NO_MEM;

  handle->internal.uart_num = uart_num;
  handle->internal.sms_queue = xQueueCreate(8, sizeof(sms_message_t));
  handle->internal.sms_cb = NULL;
  handle->internal.pwrkey_gpio = pwrkey_pin;
  handle->internal.status_gpio = status_pin;
  handle->internal.ri_gpio = ri_pin;

  if (!handle->internal.sms_queue) {
    free(handle);
    return ESP_ERR_NO_MEM;
  }

  if (uart_driver_install(uart_num, UART_BUF_SIZE * 2, 0, 0, NULL, 0) != ESP_OK) {
    vQueueDelete(handle->internal.sms_queue);
    free(handle);
    return ESP_FAIL;
  }
  if (uart_param_config(uart_num, uart_config) != ESP_OK) {
    uart_driver_delete(uart_num);
    vQueueDelete(handle->internal.sms_queue);
    free(handle);
    return ESP_ERR_INVALID_ARG;
  }
  if (uart_set_pin(uart_num, txd_pin, rxd_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
    uart_driver_delete(uart_num);
    vQueueDelete(handle->internal.sms_queue);
    free(handle);
    return ESP_ERR_INVALID_ARG;
  }

  gpio_set_direction(pwrkey_pin, GPIO_MODE_OUTPUT);
  gpio_set_pull_mode(pwrkey_pin, GPIO_FLOATING);

  gpio_set_direction(status_pin, GPIO_MODE_INPUT);
  gpio_set_pull_mode(status_pin, GPIO_FLOATING);

  // if (xTaskCreate(uart_task, "sim900d_uart_task", 4096, handle, 10, &handle->internal.uart_task_handle) != pdPASS) {
  //   uart_driver_delete(uart_num);
  //   vQueueDelete(handle->internal.sms_queue);
  //   free(handle);
  //   return ESP_ERR_NO_MEM;
  // }
  if (xTaskCreate(sim900d_sms_task, "sim900d_sms_task", 4096, handle, 10, &handle->internal.sms_task_handle) !=
      pdPASS) {
    uart_driver_delete(uart_num);
    vQueueDelete(handle->internal.sms_queue);
    free(handle);
    return ESP_ERR_NO_MEM;
  }

  *out_handle = handle;
  return ESP_OK;
}

void sim900d_uart_set_callback(sim900d_uart_handle_t *handle, sms_callback_t cb) {
  if (handle) {
    handle->internal.sms_cb = cb;
  }
}

void sim900d_uart_deinit(sim900d_uart_handle_t *handle) {
  if (!handle)
    return;
  if (handle->internal.uart_task_handle)
    vTaskDelete(handle->internal.uart_task_handle);
  if (handle->internal.sms_task_handle)
    vTaskDelete(handle->internal.sms_task_handle);
  uart_driver_delete(handle->internal.uart_num);
  if (handle->internal.sms_queue)
    vQueueDelete(handle->internal.sms_queue);
  free(handle);
}