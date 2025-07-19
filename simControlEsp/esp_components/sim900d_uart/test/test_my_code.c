#include "unity.h"

void setUp(void) {
    // выполняется перед каждым тестом
}

void tearDown(void) {
    // выполняется после каждого теста
}

void test_add_should_return_sum(void) {
    TEST_ASSERT_EQUAL(5, add(2, 3));
}

void test_subtract_should_return_difference(void) {
    TEST_ASSERT_EQUAL(1, subtract(3, 2));
}
