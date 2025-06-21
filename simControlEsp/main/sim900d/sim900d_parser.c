#include <stdio.h>
#include <string.h>


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

    // Ищем префикс, который может быть в любом месте строки, но всегда предваряется началом строки или символом новой строки
    int foundIndex = -1;
    const char *paramStart = NULL;
    for (int i = 0; i < handlerCount; i++) {
        const char *prefix = handlerPrefixes[i];
        size_t prefixLen = strlen(prefix);

#ifdef SIM900D_VERBOSE
        ESP_LOGV(TAG, "Checking prefix[%d]: \"%s\"", i, prefix);
#endif

        // Ищем вхождение префикса, предваряемое началом строки или '\n'
        const char *search = line;
        while (search) {
            // Найти вхождение префикса
            const char *pos = strstr(search, prefix);
            if (!pos) break;
            // Проверить, что перед префиксом либо начало строки, либо '\n'
            if (pos == line || *(pos - 1) == '\n') {
#ifdef SIM900D_VERBOSE
                ESP_LOGV(TAG, "Prefix match found: \"%s\" at index %d", prefix, i);
#endif
                foundIndex = i;
                paramStart = pos + prefixLen;
                break;
            }
            // Продолжаем поиск после текущего вхождения
            search = pos + 1;
        }
        if (foundIndex != -1) break;
    }

    if (foundIndex != -1) {
        const char *prefix = handlerPrefixes[foundIndex];
#ifdef SIM900D_VERBOSE
        ESP_LOGV(TAG, "Handler found for prefix: \"%s\"", prefix);
#endif
        currentParams.paramCount = 0;
        currentParams.multilineBody[0] = '\0';

        // Пропускаем разделители после префикса
        const char *p = paramStart;
        while (*p == ':' || *p == ' ' || *p == '\t')
            p++;

        char buffer[MAX_PARAM_LEN];
        int bufIndex = 0;
        int inQuote = 0;

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

        if (strcmp(prefix, SIM900D_RESP_CMGR) == 0 || strcmp(prefix, SIM900D_RESP_CMGL) == 0) {
#ifdef SIM900D_VERBOSE
            ESP_LOGV(TAG, "Switching to multiline accumulation for prefix: \"%s\"", prefix);
#endif
            currentState = STATE_ACCUMULATING_MULTILINE;
            multilineBuffer[0] = '\0';
            currentHandler = handlerTable[foundIndex];
        } else {
#ifdef SIM900D_VERBOSE
            ESP_LOGV(TAG, "Invoking handler for prefix: \"%s\"", prefix);
#endif
            handlerTable[foundIndex](&currentParams);
        }
        return;
    }

#ifdef SIM900D_VERBOSE
    ESP_LOGV(TAG, "Unrecognized line: \"%s\"", line);
#endif
    // Неизвестная строка — опционально: логировать
    // printf("[SIM900D] Unrecognized: %s\n", line);
}
