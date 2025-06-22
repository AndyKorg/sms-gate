#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sim900d_command.h"
#include "sim900d_parser.h"

#define SIM900D_VERBOSE
#ifdef SIM900D_VERBOSE
#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#endif

static const char *TAG = "PARSER";

/**
 * @brief Состояния парсера для обработки однострочных и многострочных ответов.
 */
typedef enum { STATE_IDLE, STATE_ACCUMULATING_MULTILINE } ParserState;

static ParserState currentState = STATE_IDLE;
static Sim900dHandler handlerTable[MAX_HANDLERS];
static const char *handlerPrefixes[MAX_HANDLERS];
static int handlerCount = 0;

static char multilineBuffer[1024];
static Sim900dHandler currentHandler = NULL;
static Sim900dParsedParams currentParams;

// Очередь для передачи задач парсера
#define SIM900D_HANDLER_QUEUE_LEN 8

typedef struct {
  Sim900dHandler handler;
  Sim900dParsedParams params;
} sim900d_handler_task_msg_t;

static QueueHandle_t sim900d_handler_queue = NULL;

static void sim900d_handler_task(void *pvParameters) {
  sim900d_handler_task_msg_t msg;
  while (1) {
    if (xQueueReceive(sim900d_handler_queue, &msg, portMAX_DELAY) == pdTRUE) {
      if (msg.handler) {
        msg.handler(&msg.params);
      }
    }
  }
}

void sim900d_parser_init() {
#if CONFIG_LOG_DEFAULT_LEVEL > 1
  esp_log_level_set(TAG, LOG_LOCAL_LEVEL);
#endif

  handlerCount = 0;
  currentState = STATE_IDLE;
  currentHandler = NULL;
  multilineBuffer[0] = '\0';
  for (int i = 0; i < MAX_HANDLERS; i++) {
    handlerPrefixes[i] = NULL;
  }

  if (!sim900d_handler_queue) {
    sim900d_handler_queue = xQueueCreate(SIM900D_HANDLER_QUEUE_LEN, sizeof(sim900d_handler_task_msg_t));
    xTaskCreate(sim900d_handler_task, "sim900d_handler_task", 2048, NULL, 8, NULL);
  }
}

void sim900d_register_handler(const char *prefix, Sim900dHandler handler) {
  for (int i = 0; i < handlerCount; i++) {
    if (strcmp(handlerPrefixes[i], prefix) == 0) {
      if (handler == NULL) {
        // Удаляем обработчик и сдвигаем массивы
        for (int j = i; j < handlerCount - 1; j++) {
          handlerPrefixes[j] = handlerPrefixes[j + 1];
          handlerTable[j] = handlerTable[j + 1];
        }
        handlerCount--;
      } else {
        handlerTable[i] = handler;
      }
      return;
    }
  }
  if (handler != NULL && handlerCount < MAX_HANDLERS) {
    handlerPrefixes[handlerCount] = prefix;
    handlerTable[handlerCount] = handler;
    handlerCount++;
  }
}

// Обработка однострочных ответов
static void sim900d_handle_singleline_response(const char *prefix, const char *p, int foundIndex) {
  char buffer[MAX_PARAM_LEN];
  int bufIndex = 0;
  int paramIndex = 0;
  int inQuote = 0;

  currentParams.paramCount = 0;
  currentParams.multilineBody[0] = '\0';

  while (*p && *p != '\r' && *p != '\n' && paramIndex < MAX_PARAMS) {
    if (*p == '"') {
      inQuote = !inQuote;
    } else if (*p == ',' && !inQuote) {
      buffer[bufIndex] = '\0';
      strncpy(currentParams.params[paramIndex], buffer, MAX_PARAM_LEN);
#ifdef SIM900D_VERBOSE
      ESP_LOGV(TAG, "Param[%d]: \"%s\"", paramIndex, buffer);
#endif
      paramIndex++;
      bufIndex = 0;
    } else {
      if (bufIndex < MAX_PARAM_LEN - 1) {
        buffer[bufIndex++] = *p;
      }
    }
    p++;
  }
  // Добавляем последний параметр, если есть
  if (bufIndex > 0 && paramIndex < MAX_PARAMS) {
    buffer[bufIndex] = '\0';
    strncpy(currentParams.params[paramIndex], buffer, MAX_PARAM_LEN);
#ifdef SIM900D_VERBOSE
    ESP_LOGV(TAG, "Param[%d]: \"%s\"", paramIndex, buffer);
#endif
    paramIndex++;
  }
  currentParams.paramCount = paramIndex;

#ifdef SIM900D_VERBOSE
  ESP_LOGV(TAG, "Invoking handler for prefix: \"%s\"", prefix);
#endif
  sim900d_handler_task_msg_t msg = {0};
  msg.handler = handlerTable[foundIndex];
  msg.params = currentParams;
  if (sim900d_handler_queue) {
    xQueueSend(sim900d_handler_queue, &msg, 0);
  }
}

// Функция поиска префикса и возврата индекса и позиции параметров
static int sim900d_find_prefix(const char *line, const char **paramStart) {
  for (int i = 0; i < handlerCount; i++) {
    const char *prefix = handlerPrefixes[i];
    size_t prefixLen = strlen(prefix);

#ifdef SIM900D_VERBOSE
    ESP_LOGV(TAG, "Checking prefix[%d]: \"%s\"", i, prefix);
#endif

    const char *search = line;
    while (search) {
      const char *pos = strstr(search, prefix);
      if (!pos)
        break;
      if (pos == line || *(pos - 1) == '\n') {
#ifdef SIM900D_VERBOSE
        ESP_LOGV(TAG, "Prefix match found: \"%s\" at index %d", prefix, i);
#endif
        if (paramStart) *paramStart = pos + prefixLen;
        return i;
      }
      search = pos + 1;
    }
  }
  if (paramStart) *paramStart = NULL;
  return -1;
}

void sim900d_parse_line(const char *line) {
#ifdef SIM900D_VERBOSE
  ESP_LOGV(TAG, "Parsing line: \"%s\"", line);
#endif

  if (currentState == STATE_ACCUMULATING_MULTILINE) {
#ifdef SIM900D_VERBOSE
    ESP_LOGV(TAG, "Accumulating multiline: \"%s\"", line);
#endif
    if (strcmp(line, SIM900D_RESP_OK) == 0 || strcmp(line, SIM900D_RESP_ERROR) == 0) {
      strncpy(currentParams.multilineBody, multilineBuffer, sizeof(currentParams.multilineBody));
#ifdef SIM900D_VERBOSE
      ESP_LOGV(TAG, "Multiline end detected. Handler: %p", (void *)currentHandler);
#endif
      if (currentHandler) {
        currentHandler(&currentParams);
      }
      currentState = STATE_IDLE;
      multilineBuffer[0] = '\0';
      currentHandler = NULL;
      return;
    }

    strncat(multilineBuffer, line, sizeof(multilineBuffer) - strlen(multilineBuffer) - 2);
    strncat(multilineBuffer, "\n", sizeof(multilineBuffer) - strlen(multilineBuffer) - 2);
    return;
  }

  const char *paramStart = NULL;
  int foundIndex = sim900d_find_prefix(line, &paramStart);

  if (foundIndex != -1) {
    const char *prefix = handlerPrefixes[foundIndex];
#ifdef SIM900D_VERBOSE
    ESP_LOGV(TAG, "Handler found for prefix: \"%s\"", prefix);
#endif
    // Пропускаем разделители после префикса
    const char *p = paramStart;
    while (*p == ':' || *p == ' ' || *p == '\t')
      p++;

    // Если это многострочный ответ, переходим в режим накопления
    if (strcmp(prefix, SIM900D_RESP_CMGR) == 0 || strcmp(prefix, SIM900D_RESP_CMGL) == 0) {
#ifdef SIM900D_VERBOSE
      ESP_LOGV(TAG, "Switching to multiline accumulation for prefix: \"%s\"", prefix);
#endif
      currentState = STATE_ACCUMULATING_MULTILINE;
      multilineBuffer[0] = '\0';
      currentHandler = handlerTable[foundIndex];

      // Для многострочного ответа парсим параметры как обычно
      char buffer[MAX_PARAM_LEN];
      int bufIndex = 0;
      int inQuote = 0;

      currentParams.paramCount = 0;
      currentParams.multilineBody[0] = '\0';

      while (*p && currentParams.paramCount < MAX_PARAMS) {
        if (*p == '"') {
          inQuote = !inQuote;
        } else if (*p == ',' && !inQuote) {
          buffer[bufIndex] = '\0';
          strncpy(currentParams.params[currentParams.paramCount], buffer, MAX_PARAM_LEN);
#ifdef SIM900D_VERBOSE
          ESP_LOGV(TAG, "Param[%d]: \"%s\"", currentParams.paramCount, buffer);
#endif
          currentParams.paramCount++;
          bufIndex = 0;
        } else {
          if (bufIndex < MAX_PARAM_LEN - 1) {
            buffer[bufIndex++] = *p;
          }
        }
        p++;
      }

      if (bufIndex > 0 && currentParams.paramCount < MAX_PARAMS) {
        buffer[bufIndex] = '\0';
        strncpy(currentParams.params[currentParams.paramCount], buffer, MAX_PARAM_LEN);
#ifdef SIM900D_VERBOSE
        ESP_LOGV(TAG, "Param[%d]: \"%s\"", currentParams.paramCount, buffer);
#endif
        currentParams.paramCount++;
      }
      return;
    } else {
      // Для однострочного ответа вызываем отдельную функцию
      sim900d_handle_singleline_response(prefix, p, foundIndex);
      return;
    }
  }

#ifdef SIM900D_VERBOSE
  ESP_LOGV(TAG, "Unrecognized line: \"%s\"", line);
#endif
}