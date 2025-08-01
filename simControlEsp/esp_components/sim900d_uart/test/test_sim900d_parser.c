#include "../sim900d_parser.h"
#include "unity.h"
#include <stdio.h>


// Мок-обработчик для проверки вызова
static Sim900dParsedParams last_params;
static bool handler_called = false;

static void test_cmgr_handler(Sim900dParsedParams *params) {
  handler_called = true;
  memcpy(&last_params, params, sizeof(Sim900dParsedParams));
  printf("handle test\n");

  printf("multilineBody codes: ");
  for (int i = 0; i < sizeof(params->multilineBody); ++i) {
    char c = params->multilineBody[i];
    if (c == '\0')
      break; // завершение строки
    printf("%02X ", (unsigned char)c);
  }
  printf("\n");
}

TEST_CASE("PDU Parser Test", "[sim900d]") {
  sim900d_parser_init();
  sim900d_sms_mode_t mode = SMS_MODE_PDU;
  sim900d_sms_mode(NULL, mode, true);
  sim900d_register_handler("+CMGR:", test_cmgr_handler);

  const char *test_line = "+CMGR: "
                          "0,\"\","
                          "161\r\n07919712690080F86010D04D63841E96A7CD660008527052808272218C0500039F0401042704350440043"
                          "504370020003300200434043D044F00200441043F04380448043504420441044F0020043F043B043004420430002"
                          "00437043000200432043004880020044204300440043804440020002D00200036003500300020044004430431002"
                          "E000D000A000D000A041204300448002004310430043B0430043D0441003A00200032\r\n\r\nOK\r\n";

  handler_called = false;
  parse_state_t state = sim900d_parse_line(test_line);

  TEST_ASSERT_EQUAL(PARSE_STATE_DONE, state);
  TEST_ASSERT_TRUE(handler_called);

  // Проверяем, что параметры корректно разобраны
  TEST_ASSERT_NOT_EQUAL(0, last_params.paramCount);
  TEST_ASSERT_NOT_EQUAL('\0', last_params.multilineBody[0]);
  TEST_ASSERT_EQUAL(SMS_MODE_PDU, last_params.sms_mode);

  // Вывод для отладки
  printf("paramCount: %d\n", last_params.paramCount);
  printf("multilineBody: %s\n", last_params.multilineBody);
  printf("sms_mode: %d\n", last_params.sms_mode);
}
