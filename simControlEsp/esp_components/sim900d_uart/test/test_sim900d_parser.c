#include "unity.h"
#include "sim900d_parser.h"

void test_header_line_ok(void) {
  printf("test ok");
}

void test_sim900d_parser(void) {
    RUN_TEST(test_header_line_ok);
}
