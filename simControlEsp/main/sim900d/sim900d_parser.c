#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
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

// Обработка однострочных ответов
static void sim900d_handle_singleline_response(const char *prefix, const char *p, int foundIndex) {
  char buffer[MAX_PARAM_LEN];
  int bufIndex = 0;
  int paramIndex = 0;
  int inQuote = 0;
  int parenLevel = 0;

  currentParams.paramCount = 0;
  currentParams.multilineBody[0] = '\0';

  while (*p && *p != '\r' && *p != '\n' && paramIndex < MAX_PARAMS) {
    if (*p == '"') {
      inQuote = !inQuote;
    } else if (!inQuote) {
      if (*p == '(') {
        parenLevel++;
      } else if (*p == ')') {
        if (parenLevel > 0)
          parenLevel--;
      }
    }

    if (*p == ',' && !inQuote && parenLevel == 0) {
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

  // Проверяем наличие "OK" или "ERROR" в конце строки
  // Ищем последние непустые символы после параметров
  while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t')
    p++;
  if (strncmp(p, SIM900D_RESP_ERROR, sizeof(SIM900D_RESP_ERROR) - 1) == 0) {
#ifdef SIM900D_VERBOSE
    ESP_LOGV(TAG, "Line ends with ERROR");
#endif
    currentParams.result = false;
  }
  if (strncmp(p, SIM900D_RESP_OK, sizeof(SIM900D_RESP_OK) - 1) == 0) {
#ifdef SIM900D_VERBOSE
    ESP_LOGV(TAG, "Line ends with OK");
#endif
    currentParams.result = true;
  }

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
        if (paramStart)
          *paramStart = pos + prefixLen;
        return i;
      }
      search = pos + 1;
    }
  }
  if (paramStart)
    *paramStart = NULL;
  return -1;
}

static void trim_whitespace(char *dst, const char *src) {
  while (*src && isspace((unsigned char)*src))
    src++;
  const char *end = src + strlen(src);
  while (end > src && isspace((unsigned char)*(end - 1)))
    end--;
  size_t len = end - src;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

/**
 * @brief Проверяет, является ли строка допустимым UCS2 (UTF-16BE) HEX-представлением.
 */
static bool is_valid_ucs2_hex(const char *str) {
  size_t len = strlen(str);
  if (len < 4 || len % 4 != 0)
    return false;

  for (size_t i = 0; i < len; ++i) {
    if (!isxdigit((unsigned char)str[i])) {
      return false; // Не HEX-цифра
    }
  }

  for (size_t i = 0; i < len; i += 4) {
    // Преобразуем 4 HEX-символа в 2 байта
    char hex_byte[5] = {0}; // 4 символа + \0
    hex_byte[0] = str[i];
    hex_byte[1] = str[i + 1];
    hex_byte[2] = '\0';
    uint8_t high;
    if (sscanf(hex_byte, "%2hhx", &high) != 1)
      return false;

    hex_byte[0] = str[i + 2];
    hex_byte[1] = str[i + 3];
    hex_byte[2] = '\0';
    uint8_t low;
    if (sscanf(hex_byte, "%2hhx", &low) != 1)
      return false;

    uint16_t code_unit = (high << 8) | low;

    // Проверка недопустимых диапазонов UCS2 (UTF-16)
    if ((code_unit >= 0xD800 && code_unit <= 0xDFFF) || // суррогаты
        code_unit == 0xFFFE || code_unit == 0xFFFF) {
      return false;
    }
  }

  return true;
}

/**
 * @brief Преобразует строку в формате UCS2 HEX (UTF-16BE) в UTF-8.
 * @param hex_str Входная строка, например "041F04400438043204350442"
 * @return malloc-строка с результатом в UTF-8, нужно освободить через free(), или NULL при ошибке.
 */
static char *ucs2_hex_to_utf8(const char *hex_str) {
  if (!hex_str)
    return NULL;

  size_t hex_len = strlen(hex_str);
  if (hex_len % 4 != 0)
    return NULL; // каждый символ — 4 hex-цифры (2 байта)

  size_t max_chars = hex_len / 4;
  size_t max_utf8_len = max_chars * 3 + 1; // UTF-8 до 3 байт на символ
  char *utf8_str = malloc(max_utf8_len);
  if (!utf8_str)
    return NULL;

  size_t utf8_index = 0;
  for (size_t i = 0; i < hex_len; i += 4) {
    char hex_code[5] = {hex_str[i], hex_str[i + 1], hex_str[i + 2], hex_str[i + 3], '\0'};
    uint16_t code_unit;
    if (sscanf(hex_code, "%4hx", &code_unit) != 1) {
      free(utf8_str);
      return NULL;
    }

    if (code_unit <= 0x7F) {
      utf8_str[utf8_index++] = (char)code_unit;
    } else if (code_unit <= 0x7FF) {
      utf8_str[utf8_index++] = 0xC0 | ((code_unit >> 6) & 0x1F);
      utf8_str[utf8_index++] = 0x80 | (code_unit & 0x3F);
    } else {
      utf8_str[utf8_index++] = 0xE0 | ((code_unit >> 12) & 0x0F);
      utf8_str[utf8_index++] = 0x80 | ((code_unit >> 6) & 0x3F);
      utf8_str[utf8_index++] = 0x80 | (code_unit & 0x3F);
    }
  }

  utf8_str[utf8_index] = '\0';
  return utf8_str;
}

static parse_state_t sim900d_handle_multiline_response_start(const char *prefix, const char *p, int foundIndex) {
  currentState = STATE_ACCUMULATING_MULTILINE;
  multilineBuffer[0] = '\0';
  currentHandler = handlerTable[foundIndex];

  char buffer[MAX_PARAM_LEN];
  int bufIndex = 0;
  int inQuote = 0;

  currentParams.paramCount = 0;
  currentParams.multilineBody[0] = '\0';
  currentParams.result = true;

  // Парсим параметры
  while (*p && currentParams.paramCount < MAX_PARAMS) {
    if (*p == '"') {
      inQuote = !inQuote;
    } else if (*p == ',' && !inQuote) {
      buffer[bufIndex] = '\0';
      strncpy(currentParams.params[currentParams.paramCount], buffer, MAX_PARAM_LEN - 1);
      currentParams.params[currentParams.paramCount][MAX_PARAM_LEN - 1] = '\0';
#ifdef SIM900D_VERBOSE
      ESP_LOGV(TAG, "Param[%d]: \"%s\"", currentParams.paramCount, buffer);
#endif
      currentParams.paramCount++;
      bufIndex = 0;
    } else if ((*p == '\r' || *p == '\n') && !inQuote) {
      break;
    } else {
      if (bufIndex < MAX_PARAM_LEN - 1) {
        buffer[bufIndex++] = *p;
      }
    }
    p++;
  }

  if (bufIndex > 0 && currentParams.paramCount < MAX_PARAMS) {
    buffer[bufIndex] = '\0';
    strncpy(currentParams.params[currentParams.paramCount], buffer, MAX_PARAM_LEN - 1);
    currentParams.params[currentParams.paramCount][MAX_PARAM_LEN - 1] = '\0';
#ifdef SIM900D_VERBOSE
    ESP_LOGV(TAG, "Param[%d]: \"%s\"", currentParams.paramCount, buffer);
#endif
    currentParams.paramCount++;
  }

  while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t')
    p++;

  if (*p) {
    strncpy(multilineBuffer, p, sizeof(multilineBuffer) - 1);
    multilineBuffer[sizeof(multilineBuffer) - 1] = '\0';
  } else {
    multilineBuffer[0] = '\0';
  }

#ifdef SIM900D_VERBOSE
  ESP_LOGV(TAG, "Multiline start buffer: \"%s\"", multilineBuffer);
#endif

  return PARSE_STATE_IN_PROGRESS;
}

static parse_state_t sim900d_handle_multiline_response_continue(const char *line) {
  char cleaned[64];
  trim_whitespace(cleaned, line);

#ifdef SIM900D_VERBOSE
  ESP_LOGV(TAG, "Multiline continuation, cleaned: \"%s\"", cleaned);
#endif

  if (strcmp(cleaned, SIM900D_RESP_OK) == 0 || strcmp(cleaned, SIM900D_RESP_ERROR) == 0) {
    size_t len = strlen(multilineBuffer);
    while (len > 0 && (multilineBuffer[len - 1] == '\n' || multilineBuffer[len - 1] == '\r'))
      multilineBuffer[--len] = '\0';

    // --- Исправление: выделяем только первую строку (HEX-текст SMS) ---
    char *body_end = strpbrk(multilineBuffer, "\r\n");
    char sms_body[MAX_PARAM_LEN * 4] = {0};
    if (body_end) {
      size_t body_len = body_end - multilineBuffer;
      if (body_len >= sizeof(sms_body))
        body_len = sizeof(sms_body) - 1;
      strncpy(sms_body, multilineBuffer, body_len);
      sms_body[body_len] = '\0';
    } else {
      strncpy(sms_body, multilineBuffer, sizeof(sms_body) - 1);
      sms_body[sizeof(sms_body) - 1] = '\0';
    }

    strncpy(currentParams.multilineBody, sms_body, sizeof(currentParams.multilineBody) - 1);
    currentParams.multilineBody[sizeof(currentParams.multilineBody) - 1] = '\0';

    if (is_valid_ucs2_hex(currentParams.multilineBody)) {
      char *utf8 = ucs2_hex_to_utf8(currentParams.multilineBody);
      if (utf8) {
#ifdef SIM900D_VERBOSE
        ESP_LOGV(TAG, "Decoded UCS2 to UTF-8: %s", utf8);
#endif
        strncpy(currentParams.multilineBody, utf8, sizeof(currentParams.multilineBody) - 1);
        currentParams.multilineBody[sizeof(currentParams.multilineBody) - 1] = '\0';
        free(utf8);
      } else {
#ifdef SIM900D_VERBOSE
        ESP_LOGW(TAG, "UCS2 decode failed, sending raw HEX");
#endif
      }
    } else {
#ifdef SIM900D_VERBOSE
      ESP_LOGV(TAG, "Multiline body is plain text: %s", currentParams.multilineBody);
#endif
#ifdef SIM900D_VERBOSE
      ESP_LOGV(TAG, "Multiline end detected. Handler: %p", (void *)currentHandler);
#endif
    }

    if (currentHandler) {
      currentHandler(&currentParams);
    }

    currentState = STATE_IDLE;
    multilineBuffer[0] = '\0';
    currentHandler = NULL;
    return PARSE_STATE_DONE;
  }

  size_t mlen = strlen(multilineBuffer);
  size_t llen = strlen(line);
  if (mlen + llen + 2 < sizeof(multilineBuffer)) {
    strcat(multilineBuffer, line);
    strcat(multilineBuffer, "\n");
  }

  return PARSE_STATE_IN_PROGRESS;
}

int *sim900d_parse_number_list(const char *str, int *outCount) {
  int *numbers = NULL;
  int count = 0;
  int capacity = 8;

  if (!str || !outCount)
    return NULL;

  // Пропускаем пробелы и открывающую скобку
  while (*str && (*str == ' ' || *str == '\t' || *str == '('))
    str++;

  numbers = (int *)malloc(capacity * sizeof(int));
  if (!numbers)
    return NULL;

  while (*str && *str != ')') {
    // Пропускаем пробелы
    while (*str == ' ' || *str == '\t')
      str++;

    // Читаем первое число
    char *endptr;
    int start = (int)strtol(str, &endptr, 10);
    if (endptr == str)
      break; // Не число

    str = endptr;

    // Проверяем на диапазон
    if (*str == '-') {
      str++;
      int end = (int)strtol(str, &endptr, 10);
      if (endptr == str)
        break; // Не число после '-'
      str = endptr;
      if (end >= start) {
        for (int v = start; v <= end; v++) {
          if (count >= capacity) {
            capacity *= 2;
            numbers = (int *)realloc(numbers, capacity * sizeof(int));
            if (!numbers)
              return NULL;
          }
          numbers[count++] = v;
        }
      }
    } else {
      if (count >= capacity) {
        capacity *= 2;
        numbers = (int *)realloc(numbers, capacity * sizeof(int));
        if (!numbers)
          return NULL;
      }
      numbers[count++] = start;
    }

    // Пропускаем пробелы и запятые
    while (*str == ' ' || *str == '\t')
      str++;
    if (*str == ',')
      str++;
  }

  *outCount = count;
  return numbers;
}

parse_state_t sim900d_parse_line(const char *line) {
#ifdef SIM900D_VERBOSE
  ESP_LOGV(TAG, "Parsing line: \"%s\"", line);
#endif

  if (currentState == STATE_ACCUMULATING_MULTILINE) {
    return sim900d_handle_multiline_response_continue(line);
  }

  const char *paramStart = NULL;
  int foundIndex = sim900d_find_prefix(line, &paramStart);

  if (foundIndex != -1) {
    const char *prefix = handlerPrefixes[foundIndex];
#ifdef SIM900D_VERBOSE
    ESP_LOGV(TAG, "Handler found for prefix: \"%s\"", prefix);
#endif
    const char *p = paramStart;
    while (*p == ':' || *p == ' ' || *p == '\t')
      p++;

    if (strcmp(prefix, SIM900D_RESP_CMGR) == 0 || strcmp(prefix, SIM900D_RESP_CMGL) == 0) {
#ifdef SIM900D_VERBOSE
      ESP_LOGV(TAG, "Switching to multiline accumulation for prefix: \"%s\"", prefix);
#endif
      return sim900d_handle_multiline_response_start(prefix, p, foundIndex);
    } else {
      sim900d_handle_singleline_response(prefix, p, foundIndex);
      return PARSE_STATE_DONE;
    }
  }

#ifdef SIM900D_VERBOSE
  ESP_LOGV(TAG, "Unrecognized line: \"%s\"", line);
#endif
  return PARSE_STATE_DONE;
}

esp_err_t sim900d_register_handler(const char *prefix, Sim900dHandler handler) {
  if (!prefix || prefix[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }
  for (int i = 0; i < handlerCount; i++) {
    if (strcmp(handlerPrefixes[i], prefix) == 0) {
      if (handler == NULL) {
        // Удаляем обработчик и сдвигаем массивы
        for (int j = i; j < handlerCount - 1; j++) {
          handlerPrefixes[j] = handlerPrefixes[j + 1];
          handlerTable[j] = handlerTable[j + 1];
        }
        handlerCount--;
        return ESP_OK;
      } else {
        handlerTable[i] = handler;
        return ESP_OK;
      }
    }
  }
  if (handler != NULL && handlerCount < MAX_HANDLERS) {
    handlerPrefixes[handlerCount] = prefix;
    handlerTable[handlerCount] = handler;
    handlerCount++;
    return ESP_OK;
  }
  return ESP_ERR_NO_MEM;
}

esp_err_t sim900d_parser_init() {
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
    if (!sim900d_handler_queue) {
#ifdef SIM900D_VERBOSE
      ESP_LOGE(TAG, "Failed to create handler queue");
#endif
      return ESP_ERR_NO_MEM;
    }
    BaseType_t task_created = xTaskCreate(sim900d_handler_task, "sim900d_handler_task", 2048 * 2, NULL, 8, NULL);
    if (task_created != pdPASS) {
#ifdef SIM900D_VERBOSE
      ESP_LOGE(TAG, "Failed to create handler task");
#endif
      vQueueDelete(sim900d_handler_queue);
      sim900d_handler_queue = NULL;
      return ESP_ERR_NO_MEM;
    }
  }
  return ESP_OK;
}
