#include "unity.h"

void test_sim900d_parser(void){
    TEST_ASSERT_EQUAL(1, 1);

} // Группируй тесты по функциям


void app_main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sim900d_parser); // или вызов отдельных RUN_TEST(...)
    UNITY_END();
}

// Если нужен интерактивный режим, то нужно добавить
// void test_sim900d_parser(void) {
//     TEST_ASSERT_EQUAL(1, 1);
// }

// void test_sim900d_another(void) {
//     TEST_ASSERT_TRUE(true);
// }

// void app_main(void) {
//     const unity_test_fn_t test_funcs[] = {
//         test_sim900d_parser,
//         test_sim900d_another
//     };
//     const char* test_names[] = {
//         "Parser Test",
//         "Another Test"
//     };

//     unity_run_tests_by_name(test_names, test_funcs, 2);
// }

