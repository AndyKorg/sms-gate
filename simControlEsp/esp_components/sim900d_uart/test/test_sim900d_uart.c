#include "unity.h"
#include "mocks\mock_uart.h"
//#include "mock_event_groups.h"
//#include "mock_freertos.h"
#include "../sim900d_uart_internal.h"
#include "sim900d_uart.h"

EventGroupHandle_t mocked_event_group;

// Флаг-состояния для моков
static uint32_t event_bits = 0;

void setUp(void) {
    event_bits = 0;
    mocked_event_group = (EventGroupHandle_t)0x1234;
}

void tearDown(void) {}

/// --- Тест 1: Только один может использовать UART ---
void test_exclusive_uart_access(void) {
    // Первое выполнение команды: открыло UART
    uart_write_bytes_ExpectAndReturn(UART_NUM, "AT", 2, 2);
    uart_write_bytes_ExpectAndReturn(UART_NUM, "\r\n", 2, 2);
    uart_read_bytes_ExpectAnyArgsAndReturn(0);

    // Должно не повлиять на другие вызовы, UART занят
    int len = sim900d_send_at("AT", NULL, 0, 1000);
    TEST_ASSERT_EQUAL(0, len);
}

/// --- Тест 2: Вызов без response, разрешает прием ---
void test_send_without_response_allows_uart_read(void) {
    // Clear BUSY, write command
    xEventGroupClearBits_ExpectAndReturn(mocked_event_group, SIM900D_UART_EVENT_BUSY, 0);
    uart_write_bytes_ExpectAndReturn(UART_NUM, "AT", 2, 2);
    uart_write_bytes_ExpectAndReturn(UART_NUM, "\r\n", 2, 2);
    uart_read_bytes_ExpectAnyArgsAndReturn(0); // нет чтения

    // Подтверждение, что команда прошла и uart_read_task может читать
    int len = sim900d_send_at("AT", NULL, 0, 1000);
    TEST_ASSERT_EQUAL(0, len);

    // Имитируем чтение в uart_read_task
    xEventGroupWaitBits_ExpectAndReturn(mocked_event_group, SIM900D_UART_EVENT_READ_ENABLE, 0, 1, portMAX_DELAY, SIM900D_UART_EVENT_READ_ENABLE);
    xEventGroupGetBits_ExpectAndReturn(mocked_event_group, SIM900D_UART_EVENT_READ_ENABLE);
    uart_read_bytes_ExpectAnyArgsAndReturn(10);
    xEventGroupClearBits_ExpectAndReturn(mocked_event_group, SIM900D_UART_EVENT_BUSY, 0);

    sim900d_uart_read_task(NULL);
}

/// --- Тест 3: Вызов с response блокирует uart_read_task ---
void test_send_with_response_blocks_read_task(void) {
    char buffer[64];

    // Очистка BUSY
    xEventGroupClearBits_ExpectAndReturn(mocked_event_group, SIM900D_UART_EVENT_BUSY, 0);
    uart_write_bytes_ExpectAndReturn(UART_NUM, "AT", 2, 2);
    uart_write_bytes_ExpectAndReturn(UART_NUM, "\r\n", 2, 2);
    uart_read_bytes_ExpectAnyArgsAndReturn(5);

    // Запуск команды, должна блокировать uart_read_task
    int len = sim900d_send_at("AT", buffer, sizeof(buffer), 1000);
    TEST_ASSERT_EQUAL(5, len);

    // Попытка запустить uart_read_task – бит BUSY всё ещё установлен
    xEventGroupWaitBits_ExpectAndReturn(mocked_event_group, SIM900D_UART_EVENT_READ_ENABLE, 0, 1, portMAX_DELAY, SIM900D_UART_EVENT_READ_ENABLE);
    xEventGroupGetBits_ExpectAndReturn(mocked_event_group, SIM900D_UART_EVENT_READ_ENABLE | SIM900D_UART_EVENT_BUSY); // всё ещё BUSY
    // uart_read_bytes НЕ должен вызываться (т.к. BUSY)
    sim900d_uart_read_task(NULL); // ничего не делает
}

/// --- Тест 4: uart_read_task читает, но блокируется, если не весь ответ пришел ---
void test_read_task_blocks_if_response_partial(void) {
    // Read enable
    xEventGroupWaitBits_ExpectAndReturn(mocked_event_group, SIM900D_UART_EVENT_READ_ENABLE, 0, 1, portMAX_DELAY, SIM900D_UART_EVENT_READ_ENABLE);
    xEventGroupGetBits_ExpectAndReturn(mocked_event_group, SIM900D_UART_EVENT_READ_ENABLE);

    // uart_read_bytes читает, но парсер возвращает IN_PROGRESS
    uart_read_bytes_ExpectAnyArgsAndReturn(10);
    sim900d_parse_line_ExpectAndReturn(_, PARSE_STATE_IN_PROGRESS);

    // Устанавливаем BUSY
    xEventGroupSetBits_ExpectAndReturn(mocked_event_group, SIM900D_UART_EVENT_BUSY, 0);

    sim900d_uart_read_task(NULL);
}
