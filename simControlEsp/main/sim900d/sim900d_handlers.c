#include "sim900d_handlers.h"
#include "sim900d_command.h"
#include "sim900d_parser.h"
#include "sim900d_uart_internal.h"


#include "esp_log.h"
#include <string.h>


static const char *TAG = "SIM900_HANDLER";

static int last_sms_index = SIM900D_NO_INDEX_MEM; // Запрошенный индекс СМС из памяти sim900

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
    sim900d_enqueue_lowprio_cmd(SIM900D_CMD_DELETE_ALL_SMS, pdMS_TO_TICKS(60000));
  }

  sim900d_enqueue_lowprio_cmd(SIM900D_CMD_CREG, pdMS_TO_TICKS(500));
}

static void sim900d_cmgr_handler(Sim900dParsedParams *params) {
  if (!params || params->paramCount < 4) {
    ESP_LOGE(TAG, SIM900D_RESP_CMGR " invalid params");
    return;
  }

  sms_message_t sms = {0};
  strncpy(sms.status, params->params[0], sizeof(sms.status) - 1);
  strncpy(sms.sender, params->params[1], sizeof(sms.sender) - 1);
  strncpy(sms.timestamp, params->params[3], sizeof(sms.timestamp) - 1);
  strncpy(sms.text, params->multilineBody, sizeof(sms.text) - 1);
  sms.index = last_sms_index;
  sim900d_enqueue_sms(&sms);
  last_sms_index = SIM900D_NO_INDEX_MEM;
}

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

static bool list_contains(int *list, int count, int val) {
  for (int i = 0; i < count; i++) {
    if (list[i] == val)
      return true;
  }
  return false;
}

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

void sim900d_register_handlers(void) {
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
