#include "esp_log.h"
#include "unity.h"
#include "unity_test_runner.h"
#include <stdbool.h>

static void print_test_separator(const char *test_name) {
    printf("\n");
    printf("==========================================\n");
    printf("Running test: %s\n", test_name);
    printf("==========================================\n");
}

void app_main(void) {

    // Проверим общее количество тестов
    // int total_tests = unity_get_test_count();
    // printf("Total registered tests: %d\n", total_tests);
    
    // // Выведем информацию о всех тестах
    // for (int i = 0; i < total_tests; i++) {
    //     test_desc_t test_info;
    //     if (unity_get_test_info(i, &test_info)) {
    //         printf("Test %d: '%s' - tags: '%s'\n", i, test_info.name, test_info.desc);
    //     }
    
    // }

    UNITY_BEGIN();

    unity_run_tests_by_tag("[version]", false);

    unity_run_tests_by_tag("[sim900d_parse]", false);
    
    printf("\n");
    printf("==========================================\n");
    printf("All tests completed!\n");
    printf("==========================================\n");
    
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
