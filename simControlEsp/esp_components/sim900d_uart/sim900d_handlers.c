/**
 * @brief Последовательность команд
 *
 * start => SIM900D_CMD_CPIN (pin card) -> sim900d_cpin_handler
 * => SIM900D_CMD_CPMS (memory state)  -> sim900d_cpms_handler (low SIM900D_CMGDA_PDU_DEL_ALL)
 * => SIM900D_CMD_CREG (net state) -> sim900d_creg_handler (<= repeat =>> SIM900D_CMD_CREG)
 * => SIM900D_RESP_CNMI_TEST (format sms indication) -> sim900d_cnmi_test_handler (low SIM900D_CMD_SMS_NOTIFY)
 * => wait
 *
 * Уведомление о приходе смс
 * => sim900d_cmti_handler (low SIM900D_CMD_READ_SMS_FMT)
 * => wait
 *
 * Получить СМС
 * => sim900d_cmgr_handler (отправить СМС в очередь)
 * => wait
 *
 */

#include "sim900d_handlers.h"
#include "sim900d_command.h"
#include "sim900d_parser.h"
#include "sim900d_uart_internal.h"
#include <string.h>

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"

static const char *TAG = "SIM900_HANDLER";

static int last_sms_index = SIM900D_NO_INDEX_MEM; // Запрошенный индекс СМС из памяти sim900

/**
 * Вспомогательные функции
 */

/**
 * @brief Разбирает строку вида "(1,2,5-7,10)" и возвращает массив чисел.
 *        Если встречается диапазон через '-', то добавляет все числа из диапазона.
 *
 * @param str Входная строка (например, "(1,2,5-7,10)")
 * @param outCount Указатель на переменную, куда будет записано количество чисел
 * @return int* Указатель на массив чисел (выделяется через malloc, не забудьте освободить)
 */
static int *sim900d_parse_number_list(const char *str, int *outCount) {
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

static bool list_contains(int *list, int count, int val) {
  for (int i = 0; i < count; i++) {
    if (list[i] == val)
      return true;
  }
  return false;
}

/**
 * @brief Установить режим приёма SMS (PDU или TEXT).
 *
 * @param sms_format Режим: SMS_MODE_PDU или SMS_MODE_TEXT
 * @return true если успешно, false если ошибка
 */
static bool sim900d_set_sms_mode(void) {
  char format_cmd[32];
  char response_buffer[64];
  sim900d_sms_mode_t sms_format = SMS_MODE_PDU;
  sim900d_parser_sms_mode(&sms_format, sms_format, false);
  snprintf(format_cmd, sizeof(format_cmd), SIM900D_CMD_CMGF_MODE, sms_format == SMS_MODE_PDU ? 0 : 1);
  int len = sim900d_send_at(format_cmd, response_buffer, sizeof(response_buffer), pdMS_TO_TICKS(500));
  if (len >= 0 && strstr(response_buffer, "OK") != NULL) {
    ESP_LOGI(TAG, "SIM900D: SMS mode set to %s", sms_format == SMS_MODE_PDU ? "PDU" : "TEXT");
    return true;
  }

  ESP_LOGE(TAG, "SIM900D: Failed to set SMS mode, response: %s", response_buffer);
  return false;
}

/**
 * Обработка ответов модуля
 */

/**
 * Состояние памяти СМС
 */
static void sim900d_cpms_handler(Sim900dParsedParams *params) {
  if (!params || params->paramCount < 6) {
    ESP_LOGE(TAG, SIM900D_RESP_CPMS " invalid params");
    return;
  }
  int used1 = atoi(params->params[1]);
  int total1 = atoi(params->params[2]);
  int used2 = atoi(params->params[4]);
  int total2 = atoi(params->params[5]);
  ESP_LOGI(TAG, "CPMS: used1=%d/%d, used2=%d/%d", used1, total1, used2, total2);

  if ((used1 >= total1) || (used2 >= total2)) {
    ESP_LOGW(TAG, "SMS memory full, deleting all messages...");
    char del_cmd[64];
    sim900d_sms_mode_t sms_format;
    sim900d_parser_sms_mode(&sms_format, sms_format, false);
    if (sms_format == SMS_MODE_PDU) {
      snprintf(del_cmd, sizeof(del_cmd), SIM900D_CMD_DELETE_SMS "%d\r\n",
               sim900d_cmgda_mode_pairs[SIM900D_CMGDA_PDU_DEL_ALL - 1].pdu_mode);
    } else {
      snprintf(del_cmd, sizeof(del_cmd), SIM900D_CMD_DELETE_SMS "\"%s\"\r\n",
               sim900d_cmgda_mode_pairs[SIM900D_CMGDA_PDU_DEL_ALL - 1].text_mode);
    }
    sim900d_enqueue_lowprio_cmd(del_cmd, pdMS_TO_TICKS(60000));
  }

  sim900d_enqueue_lowprio_cmd(SIM900D_CMD_CREG, pdMS_TO_TICKS(500));
}

/**
 * Получное СМС
 */
static void sim900d_cmgr_handler(Sim900dParsedParams *params) {
  if (!params) {
    ESP_LOGE(TAG, SIM900D_RESP_CMGR " invalid params");
    return;
  }
  for (int i = 0; i < params->paramCount; ++i) {
    ESP_LOGV(TAG, "param[%d]: %s", i, params->params[i]);
  }

  sms_message_t sms = {0};
  strncpy(sms.status, params->params[SMS_PARAM_STAT], sizeof(sms.status) - 1);
  strncpy(sms.sender, params->params[SMS_PARAM_SENDER], sizeof(sms.sender) - 1);
  strncpy(sms.timestamp, params->params[SMS_PARAM_TIMESTAMP], sizeof(sms.timestamp) - 1);
  sim900d_sms_mode_t mode;
  sim900d_parser_sms_mode(&mode, mode, false);
  sms.pdu_mode = mode == SMS_MODE_PDU;
  int tmp;
  sscanf(params->params[SMS_PARAM_IS_CONCAT], "%d", &tmp);
  sms.is_concat = tmp != 0;
  sscanf(params->params[SMS_PARAM_CONCAT_REF], "%d", &sms.concat_ref);
  sscanf(params->params[SMS_PARAM_CONCAT_TOTAL], "%d", &sms.concat_total);
  sscanf(params->params[SMS_PARAM_CONCAT_SEQ], "%d", &sms.concat_seq);
  strncpy(sms.smsc, params->params[SMS_PARAM_SMSC], sizeof(sms.smsc) - 1);
  memset(sms.text, 0, SIM900D_TEXT_LEN_MAX);
  strncpy(sms.text, params->multilineBody, strlen(params->multilineBody));
  sms.index = last_sms_index;
  sim900d_enqueue_sms(&sms);
  last_sms_index = SIM900D_NO_INDEX_MEM;
}

/**
 * Уведомление о приходе СМС
 */
static void sim900d_cmti_handler(Sim900dParsedParams *params) {
  if (!params || params->paramCount < 2) {
    ESP_LOGE(TAG, SIM900D_RESP_CMTI " invalid params");
    return;
  }
  int index = atoi(params->params[1]);
  last_sms_index = index;
  char cmd[32];
  snprintf(cmd, sizeof(cmd), SIM900D_CMD_READ_SMS_FMT, index);
  sim900d_enqueue_lowprio_cmd(cmd, pdMS_TO_TICKS(1000));
}

/**
 * Поддерживаемые форматы уведомления о СМС
 */
static void sim900d_cnmi_test_handler(Sim900dParsedParams *params) {
  if (!params || params->paramCount < SIM900D_CNMI_PARAM_MAX) {
    ESP_LOGE(TAG, SIM900D_RESP_CNMI_TEST " invalid params");
    return;
  }

  bool error = false;

  for (int i = 0; i < params->paramCount; ++i) {
    int count = 0;
    int *values = sim900d_parse_number_list(params->params[i], &count);

    if (!values || count == 0) {
      ESP_LOGE(TAG, "param[%d]: empty", i);
      error = true;
      continue;
    }

    switch (i) {
    case SIM900D_CNMI_PARAM_MODE:
      if (!list_contains(values, count, SIM900D_CNMI_MODE_BUFFER_URC))
        error = true;
      break;
    case SIM900D_CNMI_PARAM_MT:
      if (!list_contains(values, count, SIM900D_CNMI_MT_URC))
        error = true;
      break;
    case SIM900D_CNMI_PARAM_BM:
      if (!list_contains(values, count, SIM900D_CNMI_BM_DISABLE))
        error = true;
      break;
    case SIM900D_CNMI_PARAM_DS:
      if (!list_contains(values, count, SIM900D_CNMI_DS_DISABLE))
        error = true;
      break;
    case SIM900D_CNMI_PARAM_BFR:
      if (!list_contains(values, count, SIM900D_CNMI_BFR_DISABLE))
        error = true;
      break;
    }

    free(values);
  }

  if (!error) {
    sim900d_enqueue_lowprio_cmd(SIM900D_CMD_SMS_NOTIFY, pdMS_TO_TICKS(500));
  } else {
    ESP_LOGE(TAG, "CNMI config not supported, notifications disabled");
  }
}

/**
 * Нужен ли PIN код для работы с SIM картой
 */
static void sim900d_cpin_handler(Sim900dParsedParams *params) {
  if (!params || params->paramCount < 1) {
    ESP_LOGE(TAG, SIM900D_RESP_CPIN " invalid params");
    return;
  }

  const char *state = params->params[0];

  if (strcmp(state, SIM900D_RESP_READY) == 0) {
    ESP_LOGI(TAG, "SIM card is ready");
    sim900d_enqueue_lowprio_cmd(SIM900D_CMD_CPMS, pdMS_TO_TICKS(500));
  } else {
    ESP_LOGW(TAG, "SIM card state: %s", state);
  }
}

/**
 * Состояние регистрации в сети
 */
static void sim900d_creg_handler(Sim900dParsedParams *params) {
  if (!params || params->paramCount < 2) {
    ESP_LOGE(TAG, SIM900D_RESP_CREG " invalid params");
    return;
  }
  int n = atoi(params->params[0]);
  int stat = atoi(params->params[1]);
  ESP_LOGI(TAG, "CREG: n=%d, stat=%d", n, stat);

  EventGroupHandle_t evt = sim900d_get_event_group();

  bool registered = false;
  switch (stat) {
  case 0:
    ESP_LOGW(TAG, "Not registered, not searching for operator");
    xEventGroupClearBits(evt, SIM900D_UART_EVENT_NET_REGISTERED);
    break;
  case 1:
    ESP_LOGI(TAG, "Registered, home network");
    xEventGroupSetBits(evt, SIM900D_UART_EVENT_NET_REGISTERED);
    sim900d_set_sms_mode();
    sim900d_send_at(SIM900D_RESP_CNMI_TEST, NULL, 0, pdMS_TO_TICKS(500));
    registered = true;
    break;
  case 2:
    ESP_LOGI(TAG, "Not registered, searching for operator");
    xEventGroupClearBits(evt, SIM900D_UART_EVENT_NET_REGISTERED);
    // Продолжаем ожидание регистрации
    vTaskDelay(pdMS_TO_TICKS(1000));
    sim900d_send_at(SIM900D_CMD_CREG, NULL, 0, pdMS_TO_TICKS(500));
    break;
  case 3:
    ESP_LOGW(TAG, "Registration denied");
    xEventGroupClearBits(evt, SIM900D_UART_EVENT_NET_REGISTERED);
    break;
  case 4:
    ESP_LOGW(TAG, "Unknown registration status");
    xEventGroupClearBits(evt, SIM900D_UART_EVENT_NET_REGISTERED);
    break;
  case 5:
    ESP_LOGI(TAG, "Registered, roaming");
    xEventGroupSetBits(evt, SIM900D_UART_EVENT_NET_REGISTERED);
    registered = true;
    break;
  default:
    ESP_LOGW(TAG, "Unknown stat value: %d", stat);
    xEventGroupClearBits(evt, SIM900D_UART_EVENT_NET_REGISTERED);
    break;
  }
  sim900d_notify_network_status(registered);
}

/**
 * Регистрация функций обработчиков в парсере
 */
void sim900d_register_handlers(void) {
#if CONFIG_LOG_DEFAULT_LEVEL > 1
  esp_log_level_set(TAG, LOG_LOCAL_LEVEL);
#endif
  bool ok = true;
  ok &= sim900d_register_handler(SIM900D_RESP_CPIN, sim900d_cpin_handler) == ESP_OK;
  ok &= sim900d_register_handler(SIM900D_RESP_CREG, sim900d_creg_handler) == ESP_OK;
  ok &= sim900d_register_handler(SIM900D_RESP_SMS_NOTIFY, sim900d_cnmi_test_handler) == ESP_OK;
  ok &= sim900d_register_handler(SIM900D_RESP_CPMS, sim900d_cpms_handler) == ESP_OK;
  ok &= sim900d_register_handler(SIM900D_RESP_CMGR, sim900d_cmgr_handler) == ESP_OK;
  ok &= sim900d_register_handler(SIM900D_RESP_CMTI, sim900d_cmti_handler) == ESP_OK;

  if (!ok) {
    ESP_LOGE(TAG, "Failed to register SIM900D handlers");
  }
}
