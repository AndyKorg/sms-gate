#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_vfs_dev.h"
#include "linenoise/linenoise.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../esp_components/sim900d_uart/sim900d_command.h"
#include "../esp_components/sim900d_uart/sim900d_parser.h"
#include "../esp_components/sim900d_uart/sim900d_uart_internal.h"
#include "../esp_components/sim900d_uart/include/sim900d_ussd.h"
#include "../esp_components/sim900d_uart/include/sim900d_sms_types.h"

#define UART_NUM UART_NUM_0
#define UART_RX_BUF_SIZE 1024

static const char *TAG = "console";

/*----------------------------*
 *	list
 *----------------------------*/
static int get_params_list(int argc, char **argv) {
	char *name = get_next_param(true);
	do {
		if (name) {
			ESP_LOGI(TAG, "%s", name);
		}
		name = get_next_param(false);
	} while (name);
	return 0;
}

static void register_get_params(void) {
// @formatter:off
	const esp_console_cmd_t	cmd = {
		.command = "list",
		.help = "list parameters registered",
		.hint = NULL,
		.func = &get_params_list,
	};
// @formatter:on
	ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/*
 * cmd_parse
 */
static void sim900d_handler(Sim900dParsedParams *params) {
  if (!params || params->paramCount < 5) {
    ESP_LOGE(TAG, SIM900D_RESP_CMGR " invalid params");
    return;
  }
  for (int i = 0; i < params->paramCount; ++i) {
    ESP_LOGV(TAG, "param[%d]: %s", i, params->params[i]);
  }
  ESP_LOGV(TAG, "sms text %s", params->multilineBody);
}

static int cmd_parse(int argc, char **argv) {
  if (argc < 3) {
    printf("Usage: parse <string1> <string2>\n");
    return 1;
  }
  printf("Parsed strings: %s, %s\n", argv[1], argv[2]);
  char *prefix = argv[1];
  char *lime = argv[2];
  sim900d_register_handler(prefix, sim900d_handler);
  sim900d_parse_line(lime);
  return 0;
}

static void register_parse(void) {
  const esp_console_cmd_t parse_cmd = {
      .command = "parse",
      .help = "Parse two parameters - answer & string line",
      .hint = "<string1> <string2>",
      .func = &cmd_parse,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&parse_cmd));
}

/*
 * cmd_send_at
 */
static int cmd_send_at(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: send <AT command>\n");
    return 1;
  }

  // Объединяем аргументы в одну строку AT-команды
  char at_cmd[128] = {0};
  size_t offset = 0;
  for (int i = 1; i < argc && offset < sizeof(at_cmd) - 1; ++i) {
    offset += snprintf(at_cmd + offset, sizeof(at_cmd) - offset, "%s%s", argv[i], (i < argc - 1 ? " " : ""));
  }

  char response[512] = {0};
  int res = sim900d_send_at(at_cmd, response, sizeof(response), pdMS_TO_TICKS(3000));
  if (res > 0) {
    printf("Response:\n%s\n", response);
  } else {
    printf("Error sending AT command: %d\n", res);
  }

  return 0;
}

static void register_send(void) {
  const esp_console_cmd_t send_cmd = {
      .command = "send",
      .help = "Send AT command to SIM900D and print response",
      .hint = "<AT command>",
      .func = &cmd_send_at,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&send_cmd));
}

/*
 * cmd_ussd - команда для отправки USSD запросов
 */
static int cmd_ussd(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: ussd <type_number>\n");
    printf("Available USSD types:\n");
    printf("  0 - баланс\n");
    printf("  1 - номер телефона\n");
    printf("Ответ придет в телеграм бот\n");
    return 1;
  }

  int type_num = atoi(argv[1]);
  
  if (type_num >= USSD_TYPE_COUNT) {
    printf("Error: Invalid USSD type %d\n", type_num);
    return 1;
  }

  ussd_type_t type = (ussd_type_t)type_num;
  
  printf("Sending USSD request, type: %d\n", type_num);
  
  esp_err_t result = sim900d_ussd_send_by_type(type);
  
  if (result == ESP_OK) {
    printf("USSD request sent successfully. Response will be received via SMS callback.\n");
  } else if (result == ESP_ERR_INVALID_ARG) {
    printf("Error: USSD command not registered for type %d\n", type_num);
  } else {
    printf("Error sending USSD request: %s\n", esp_err_to_name(result));
  }

  return 0;
}

static void register_ussd(void) {
  const esp_console_cmd_t ussd_cmd = {
      .command = "ussd",
      .help = "Send USSD request by type",
      .hint = "<type_number>",
      .func = &cmd_ussd,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&ussd_cmd));
}

void register_console_commands() {
  register_parse();
  register_send();
  register_ussd();
  register_get_params();
}

/*----------------------------*
 *	vConsoleTask
 *----------------------------*/
static void vConsoleTask(void *pvParameters) { /* Prompt to be printed before each line.
                                                * This can be customized, made dynamic, etc.
                                                */
  const char *prompt = LOG_COLOR_I "esp32> " LOG_RESET_COLOR;

  while (true) {
    /* Get a line using linenoise.
     * The line is returned when ENTER is pressed.
     */
    char *line = linenoise(prompt);
    if (line == NULL) { /* Ignore empty lines */
      continue;
    }
    /* Add the command to the history */
    linenoiseHistoryAdd(line);
#if CONFIG_STORE_HISTORY
    /* Save command history to filesystem */
    linenoiseHistorySave(HISTORY_PATH);
#endif

    /* Try to run the command */
    int ret;
    esp_err_t err = esp_console_run(line, &ret);
    if (err == ESP_ERR_NOT_FOUND) {
      printf("Unrecognized command\n");
    } else if (err == ESP_ERR_INVALID_ARG) {
      // command was empty
    } else if (err == ESP_OK && ret != ESP_OK) {
      printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
    } else if (err != ESP_OK) {
      printf("Internal error: %s\n", esp_err_to_name(err));
    }
    /* linenoise allocates line buffer on the heap, so need to free it */
    linenoiseFree(line);
  }
}

/*----------------------------*
 *	initialize_console
 *----------------------------*/
void console_start(void) { /* Drain stdout before reconfiguring it */
  esp_log_level_set(TAG, ESP_LOG_INFO);

  fflush(stdout);
  fsync(fileno(stdout));
  /* Disable buffering on stdin */
  setvbuf(stdin, NULL, _IONBF, 0);
  /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
  //	esp_vfs_dev_uart_set_rx_line_endings(ESP_LINE_ENDINGS_CR); /* Move the
  // caret to the beginning of the next line on '\n' */
  //	esp_vfs_dev_uart_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
  /* Configure UART. Note that REF_TICK is used so that the baud rate remains
   * correct while APB frequency is changing in light sleep mode.
   */
  // @formatter:off
  const uart_config_t uart_config = {
      .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .source_clk = UART_SCLK_REF_TICK,
  };
  // @formatter:on
  /* Install UART driver for interrupt-driven reads and writes */
  ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0));
  ESP_ERROR_CHECK(uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config));

  /* Tell VFS to use UART driver */
  uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

  /* Initialize the console */
  // @formatter:off
  esp_console_config_t console_config = {
      .max_cmdline_args = 8,
      .max_cmdline_length = 256,
  };
  // @formatter:on
  ESP_ERROR_CHECK(esp_console_init(&console_config));

  /* Configure linenoise line completion library */
  /* Enable multiline editing. If not set, long commands will scroll within
   * single line.
   */
  linenoiseSetMultiLine(1);

  /* Tell linenoise where to get command completions and hints */
  linenoiseSetCompletionCallback(&esp_console_get_completion);
  linenoiseSetHintsCallback((linenoiseHintsCallback *)&esp_console_get_hint);

  /* Set command history size */
  linenoiseHistorySetMaxLen(100);

  /* Register commands */
  esp_console_register_help_command();
#if CONFIG_LOG_COLORS
  /* Prompt to be printed before each line.
   * This can be customized, made dynamic, etc.
   */
  const char *prompt = LOG_COLOR_I "esp32> " LOG_RESET_COLOR;
#endif // CONFIG_LOG_COLORS

  printf("\n"
         "Type 'help' to get the list of commands.\n");

#ifndef CONSOLE_NO_PROBE
  /* Figure out if the terminal supports escape sequences */
  int probe_status = linenoiseProbe();
  if (probe_status) { /* zero indicates success */
    // printf("Your terminal application does not support escape sequences.");
    linenoiseSetDumbMode(1);
#if CONFIG_LOG_COLORS
    /* Since the terminal doesn't support escape sequences,
     * don't use color codes in the prompt.
     */
    prompt = "esp32> ";
#endif // CONFIG_LOG_COLORS
  }
#else
  linenoiseSetDumbMode(1);
#endif

  register_console_commands();

  xTaskCreatePinnedToCore(vConsoleTask, "vConsoleTask", 4096, NULL, 5, NULL, 1);
}
