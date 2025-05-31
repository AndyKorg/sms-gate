#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "sim900d_uart.h"

#define UART_BUF_SIZE 1024

#define SIM900D_CMD_AT "AT"
#define SIM900D_CMD_SMS_MODE "AT+CMGF=1"
#define SIM900D_CMD_SMS_NOTIFY "AT+CNMI=2,1,0,0,0"
#define SIM900D_CMD_READ_SMS_FMT "AT+CMGR=%d"

#define SIM900D_RESP_OK "OK"
#define SIM900D_RESP_ERROR "ERROR"
#define SIM900D_RESP_CMTI "+CMTI:"
#define SIM900D_RESP_CMGR "+CMGR:"

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

// Отправка AT-команды и ожидание ответа
static int sim900d_send_at(sim900d_uart_handle_t *handle, const char *cmd, char *response, size_t resp_size,
                           TickType_t timeout) {
  uart_write_bytes(handle->internal.uart_num, cmd, strlen(cmd));
  uart_write_bytes(handle->internal.uart_num, "\r\n", 2);

  if (response) {
    int len = uart_read_bytes(handle->internal.uart_num, (uint8_t *)response, resp_size - 1, timeout);
    if (len > 0) {
      response[len] = 0;
      return len;
    }
    response[0] = 0;
  }
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

bool sim900d_check_alive(sim900d_uart_handle_t *handle, uint32_t timeout_ms) {
  if (!handle)
    return false;
  char resp[64];
  int len = sim900d_send_at(handle, SIM900D_CMD_AT, resp, sizeof(resp), pdMS_TO_TICKS(timeout_ms));
  if (len > 0 && strstr(resp, SIM900D_RESP_OK))
    return true;
  return false;
}

bool sim900d_reset(sim900d_uart_handle_t *handle) {
  if (!handle)
    return false;

  gpio_num_t status_gpio = handle->internal.status_gpio;
  gpio_num_t pwrkey_gpio = handle->internal.pwrkey_gpio;
  if ((status_gpio == GPIO_NUM_NC) || (pwrkey_gpio == GPIO_NUM_NC)) {
    return false;
  }

  if (gpio_get_level(status_gpio) == 1) {
    if (sim900d_check_alive(handle, 2000)) {
      return true; // Уже включён
    }
  }

  gpio_set_level(pwrkey_gpio, 0);
  vTaskDelay(pdMS_TO_TICKS(1500));
  gpio_set_level(pwrkey_gpio, 1);

  const int timeout_ms = 10000;
  int waited = 0;
  while (gpio_get_level(status_gpio) == 0 && waited < timeout_ms) {
    vTaskDelay(pdMS_TO_TICKS(100));
    waited += 100;
  }

  return gpio_get_level(status_gpio) == 1;
}

esp_err_t sim900d_uart_init(sim900d_uart_handle_t **out_handle, uart_port_t uart_num, const uart_config_t *uart_config,
                            gpio_num_t txd_pin, gpio_num_t rxd_pin, gpio_num_t pwrkey_pin, gpio_num_t status_pin,
                            gpio_num_t ri_pin) {
  if (!out_handle || !uart_config || (txd_pin == GPIO_NUM_NC) || (rxd_pin == GPIO_NUM_NC) ||
      (pwrkey_pin == GPIO_NUM_NC) || (status_pin == GPIO_NUM_NC))
    return ESP_ERR_INVALID_ARG;

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
  gpio_set_pull_mode(status_pin, GPIO_PULLUP_ONLY);

  if (xTaskCreate(uart_task, "sim900d_uart_task", 4096, handle, 10, &handle->internal.uart_task_handle) != pdPASS) {
    uart_driver_delete(uart_num);
    vQueueDelete(handle->internal.sms_queue);
    free(handle);
    return ESP_ERR_NO_MEM;
  }
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