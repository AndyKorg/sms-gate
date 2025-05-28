#include "esp_console.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include <stdio.h>
#include <string.h>

#define PROMPT_STR "sim> "
#define UART_NUM UART_NUM_0
#define UART_RX_BUF_SIZE 1024

static const char *TAG = "console";

/* UART initialization for console */
static void uart_console_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
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

static void console_task(void *arg) {
    uart_console_init();

    esp_console_config_t console_config = {
        .max_cmdline_args = 8,
        .max_cmdline_length = 256,
        .hint_color = atoi(LOG_ANSI_COLOR_CYAN)
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    linenoiseSetMultiLine(1);
    linenoiseHistorySetMaxLen(100);

    register_console_commands();

    printf("\n"
           "Type 'help' to get the list of commands.\n"
           "Press Ctrl+D or type 'exit' to quit.\n");

    char *line;
    while (true) {
        line = linenoise(PROMPT_STR);
        if (line == NULL) { // EOF (Ctrl+D)
            break;
        }
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
            int ret;
            esp_err_t err = esp_console_run(line, &ret);
            if (err == ESP_ERR_NOT_FOUND) {
                printf("Unrecognized command\n");
            } else if (err == ESP_ERR_INVALID_ARG) {
                // command was empty
            } else if (err != ESP_OK) {
                printf("Command returned error: 0x%x\n", err);
            }
        }
        linenoiseFree(line);
    }
}

void console_start(void) {
    xTaskCreate(console_task, "console_task", 4096, NULL, 5, NULL);
}