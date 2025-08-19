#include "esp_log.h"
#include "unity.h"

static void print_test_separator(const char *test_name) {
    printf("\n");
    printf("==========================================\n");
    printf("Running test: %s\n", test_name);
    printf("==========================================\n");
}

void app_main(void) {
    UNITY_BEGIN();
    
    // PDU тесты
    print_test_separator("PDU Parser Test Latin");
    unity_run_test_by_name("PDU Parser Test Latin");
    
    print_test_separator("PDU Parser Test Rus");
    unity_run_test_by_name("PDU Parser Test Rus");

    print_test_separator("Simple Parser Test");
    unity_run_test_by_name("Simple Parser Test");
    
    print_test_separator("PDU Parser Test continue OK");
    unity_run_test_by_name("PDU Parser Test continue OK");
    
    // Тесты для обработки ошибок
    print_test_separator("Simple Response Error Test");
    unity_run_test_by_name("Simple Response Error Test");
    
    print_test_separator("Multiline Response Error Test");
    unity_run_test_by_name("Multiline Response Error Test");
    
    print_test_separator("CMGR Error During SMS Scan Test");
    unity_run_test_by_name("CMGR Error During SMS Scan Test");
    
    print_test_separator("Invalid Response Format Test");
    unity_run_test_by_name("Invalid Response Format Test");
    
    print_test_separator("Empty Response Error Test");
    unity_run_test_by_name("Empty Response Error Test");
    
    print_test_separator("CMS ERROR Response Test");
    unity_run_test_by_name("Empty Response Error Test");

    print_test_separator("Mixed OK and ERROR Test");
    unity_run_test_by_name("Mixed OK and ERROR Test");

    // Тесты для USSD
    print_test_separator("USSD Response UCS2 Test");
    unity_run_test_by_name("USSD Response UCS2 Test");
    
    print_test_separator("USSD Response Text Test");
    unity_run_test_by_name("USSD Response Text Test");
    
    print_test_separator("USSD Response Error Status Test");
    unity_run_test_by_name("USSD Response Error Status Test");
    
    print_test_separator("USSD Response Empty Message Test");
    unity_run_test_by_name("USSD Response Empty Message Test");
    
    print_test_separator("USSD Response Insufficient Params Test");
    unity_run_test_by_name("USSD Response Insufficient Params Test");

    // print_test_separator("Timeout Simulation Test");
    // unity_run_test_by_name("Timeout Simulation Test");
    
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
