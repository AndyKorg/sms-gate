#include <string.h>
#include <stdio.h>
#include "esp_err.h"

#include "sim900d_command.h"
#include "sim900d_parser.h"

/**
 * @brief Состояния парсера для обработки однострочных и многострочных ответов.
 */
typedef enum {
    STATE_IDLE,
    STATE_ACCUMULATING_MULTILINE
} ParserState;

static ParserState currentState = STATE_IDLE;
static Sim900dHandler handlerTable[MAX_HANDLERS];
static const char* handlerPrefixes[MAX_HANDLERS];
static int handlerCount = 0;

static char multilineBuffer[1024];
static Sim900dHandler currentHandler = NULL;
static Sim900dParsedParams currentParams;

void sim900d_parser_init() {
    handlerCount = 0;
    currentState = STATE_IDLE;
    currentHandler = NULL;
    multilineBuffer[0] = '\0';
    for (int i = 0; i < MAX_HANDLERS; i++) {
        handlerPrefixes[i] = NULL;
    }
}

esp_err_t sim900d_register_handler(const char* prefix, Sim900dHandler handler) {
    if (prefix == NULL) {
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

void sim900d_parse_line(const char* line) {
    if (currentState == STATE_ACCUMULATING_MULTILINE) {
        if (strcmp(line, SIM900D_CODE_OK) == 0 || strcmp(line, SIM900D_CODE_ERROR) == 0) {
            strncpy(currentParams.multilineBody, multilineBuffer, sizeof(currentParams.multilineBody));
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

    for (int i = 0; i < handlerCount; i++) {
        const char* prefix = handlerPrefixes[i];
        size_t prefixLen = strlen(prefix);

        if (strncmp(line, prefix, prefixLen) == 0) {
            currentParams.paramCount = 0;
            currentParams.multilineBody[0] = '\0';

            const char* p = line + prefixLen;
            while (*p == ':' || *p == ' ' || *p == '\t') p++;

            char buffer[MAX_PARAM_LEN];
            int bufIndex = 0;
            int inQuote = 0;

            while (*p && currentParams.paramCount < MAX_PARAMS) {
                if (*p == '"') {
                    inQuote = !inQuote;
                } else if (*p == ',' && !inQuote) {
                    buffer[bufIndex] = '\0';
                    strncpy(currentParams.params[currentParams.paramCount], buffer, MAX_PARAM_LEN);
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
                currentParams.paramCount++;
            }

            if (strcmp(prefix, "+CMGR") == 0 || strcmp(prefix, "+CMGL") == 0) {
                currentState = STATE_ACCUMULATING_MULTILINE;
                multilineBuffer[0] = '\0';
                currentHandler = handlerTable[i];
            } else {
                handlerTable[i](&currentParams);
            }
            return;
        }
    }

    // Неизвестная строка — опционально: логировать
    // printf("[SIM900D] Unrecognized: %s\n", line);
}
