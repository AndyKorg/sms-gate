#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"

static const char *TAG = "SIM900";

#include "sim900d_command.h"
#include "sim900d_parser.h"
#include "sim900d_uart.h"

typedef struct {
  uart_port_t uart_num;
  QueueHandle_t sms_queue;
  sms_callback_t sms_cb;
  TaskHandle_t uart_task_handle;
  TaskHandle_t sms_task_handle;
  gpio_num_t pwrkey_gpio;
  gpio_num_t status_gpio;
  gpio_num_t ri_gpio; // Может быть неопределен
  uint32_t flags;
  SemaphoreHandle_t flags_mutex;
} sim900d_uart_handle_t_internal;

struct sim900d_uart_handle_t {
  sim900d_uart_handle_t_internal internal;
};

#define UART_BUF_SIZE 1024

#define SIM900D_FLAG_UART_INSTALL (1 << 0) // Флаг инсталяции uart драйвера
#define SIM900D_FLAG_REGISTERED (1 << 1)   // Флаг регистрации в сети

#pragma region flagControl

/**
 * @brief Установить флаги (битовая маска)
 */
void sim900d_flags_set(sim900d_uart_handle_t *handle, uint32_t flags) {
  if (handle && handle->internal.flags_mutex) {
    xSemaphoreTake(handle->internal.flags_mutex, portMAX_DELAY);
    handle->internal.flags |= flags;
    xSemaphoreGive(handle->internal.flags_mutex);
  }
}

/**
 * @brief Сбросить флаги (битовая маска)
 */
void sim900d_flags_clear(sim900d_uart_handle_t *handle, uint32_t flags) {
  if (handle && handle->internal.flags_mutex) {
    xSemaphoreTake(handle->internal.flags_mutex, portMAX_DELAY);
    handle->internal.flags &= ~flags;
    xSemaphoreGive(handle->internal.flags_mutex);
  }
}

/**
 * @brief Получить текущее значение флагов
 */
uint32_t sim900d_flags_get(sim900d_uart_handle_t *handle) {
  uint32_t flags = 0;
  if (handle && handle->internal.flags_mutex) {
    xSemaphoreTake(handle->internal.flags_mutex, portMAX_DELAY);
    flags = handle->internal.flags;
    xSemaphoreGive(handle->internal.flags_mutex);
  }
  return flags;
}

#pragma endregion

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
    ESP_LOGV(TAG, "SIM900D->UART: <no response>");
  }
  return 0;
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

    case STATE_CREG: {
      sim900d_send_at(handle, SIM900D_CMD_CREG, NULL, 0, pdMS_TO_TICKS(500));
      if (sim900d_wait_for_response(handle, SIM900D_RESP_CREG, resp, sizeof(resp), 2000)) {
        if (sscanf(resp, "+CREG: 0,%d", &creg) == 1) {
          if (creg == 1 || creg == 5) {
            state = STATE_CGREG;
          } else if (creg == 2) {
            // Ожидание сети до 60 секунд, если модуль в режиме поиска сети
            ESP_LOGW(TAG, "Network searching, waiting up to 60 seconds...");
            int waited = 0;
            bool registered = false;
            while (waited < 60000) {
              vTaskDelay(pdMS_TO_TICKS(1000));
              waited += 1000;
              sim900d_send_at(handle, SIM900D_CMD_CREG, NULL, 0, pdMS_TO_TICKS(500));
              if (sim900d_wait_for_response(handle, SIM900D_RESP_CREG, resp, sizeof(resp), 2000)) {
                if (sscanf(resp, "+CREG: 0,%d", &creg) == 1 && (creg == 1 || creg == 5)) {
                  registered = true;
                  break;
                }
              }
            }
            if (registered) {
              ESP_LOGI(TAG, "Network registered after waiting");
              state = STATE_CGREG;
            } else {
              ESP_LOGE(TAG, "Network registration timeout");
              state = STATE_ERROR;
            }
          } else {
            ESP_LOGW(TAG, "CREG not registered: %s", resp);
            state = STATE_ERROR;
          }
        } else {
          state = STATE_ERROR;
        }
      } else {
        state = STATE_ERROR;
      }
      break;
    }

    case STATE_CGREG:
      sim900d_send_at(handle, SIM900D_CMD_CGREG, NULL, 0, pdMS_TO_TICKS(500));
      if (sim900d_wait_for_response(handle, SIM900D_RESP_CGREG, resp, sizeof(resp), 2000)) {
        if (sscanf(resp, "+CGREG: 0,%d", &cgreg) == 1 && (cgreg == 1 || cgreg == 5)) {
          state = STATE_CSQ;
        } else {
          ESP_LOGW(TAG, "CGREG not registered: %s", resp);
          // state = STATE_ERROR; GPRS Пока не важен
          state = STATE_CSQ;
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
        vTaskDelay(pdMS_TO_TICKS(5000));
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

void creg_handler(Sim900dParsedParams *params, sim900d_uart_handle_t *handle) {
  // ...парсинг params...
  // если зарегистрирован:
  sim900d_flags_set(handle, SIM900D_FLAG_REGISTERED);
  // если потеря регистрации:
  // sim900d_flags_clear(handle, SIM900D_FLAG_REGISTERED);
}

// Вспомогательная функция для освобождения ресурсов handle
static void sim900d_uart_free_handle(sim900d_uart_handle_t *handle) {
  if (!handle)
    return;
  if (handle->internal.sms_queue) {
    vQueueDelete(handle->internal.sms_queue);
    handle->internal.sms_queue = NULL;
  }
  if (sim900d_flags_get(handle) & SIM900D_FLAG_UART_INSTALL) {
    uart_driver_delete(handle->internal.uart_num);
  }
  if (handle->internal.flags_mutex)
    vSemaphoreDelete(handle->internal.flags_mutex);
  free(handle);
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
  handle->internal.flags = 0;
  handle->internal.flags_mutex = xSemaphoreCreateMutex();

  if (!handle->internal.sms_queue || !handle->internal.flags_mutex) {
    sim900d_uart_free_handle(handle);
    return ESP_ERR_NO_MEM;
  }

  if (uart_driver_install(uart_num, UART_BUF_SIZE * 2, 0, 0, NULL, 0) != ESP_OK) {
    sim900d_uart_free_handle(handle);
    return ESP_FAIL;
  }
  sim900d_flags_set(handle, SIM900D_FLAG_UART_INSTALL);
  if (uart_param_config(uart_num, uart_config) != ESP_OK) {
    sim900d_uart_free_handle(handle);
    return ESP_ERR_INVALID_ARG;
  }
  if (uart_set_pin(uart_num, txd_pin, rxd_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
    sim900d_uart_free_handle(handle);
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
    sim900d_uart_free_handle(handle);
    return ESP_ERR_NO_MEM;
  }

  *out_handle = handle;

  sim900d_register_handler();

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
  sim900d_uart_free_handle(handle);
}