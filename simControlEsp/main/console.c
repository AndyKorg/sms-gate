#include "driver/uart.h"
#include "esp_console.h"
#include "esp_log.h"
#include "linenoise/linenoise.h"
#include "esp_vfs_dev.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define UART_NUM UART_NUM_0
#define UART_RX_BUF_SIZE 1024

static const char *TAG = "console";

/* UART initialization for console */
static void uart_console_init(void) {
  const uart_config_t uart_config = {.baud_rate = 115200,
                                     .data_bits = UART_DATA_8_BITS,
                                     .parity = UART_PARITY_DISABLE,
                                     .stop_bits = UART_STOP_BITS_1,
                                     .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
  uart_param_config(UART_NUM, &uart_config);
  uart_driver_install(UART_NUM, UART_RX_BUF_SIZE * 2, 0, 0, NULL, 0);
}

/* Example command handler */
static int cmd_hello(int argc, char **argv) {
  printf("Hello, ESP-ADF Console!\n");
  return 0;
}

static void register_console_commands(void) {
  const esp_console_cmd_t hello_cmd = {
      .command = "hello",
      .help = "Print hello message",
      .hint = NULL,
      .func = &cmd_hello,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&hello_cmd));
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
  esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

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
    printf("Your terminal application does not support escape sequences.");
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
