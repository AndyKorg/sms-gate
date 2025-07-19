Юнит тесты для контроллера SIM900

Как запускать в Visual Studio Code:
1. Открыть терминал в VS Code ESP-IDF. (Ctrl+E + T)
2. Перейти в эту папку. (cd test_main)
3. Запустить тесты - сборку, прошивку (Monitor Device должен быть выключен). (idf.py build flash)
4. Запустить Monitor Device. (idf.py monitor) (Ctrl+E + M)

Стуруктура тестов:
Папка с тестами по сути отдельное приложение, со своими CMakeLists.txt, sdkconfig.h, и т.д.
В папке test_main
test_main.c - основной файл с тестами. Отсюда запускаются тесты.
Сами тесты расположены в компонетах. 
Например sim900d_uart/tests/uart_test.c - тесты для UART.

Содержимое CMakeLists.txt в корне test_main:

cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "../esp_components")

set(TEST_COMPONENTS "sim900d_uart" CACHE STRING "List of components to test")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(unit_test_test)

cmake_minimum_required(VERSION 3.16): Указывает минимально необходимую версию CMake для сборки проекта, в данном случае 3.16.

set(EXTRA_COMPONENT_DIRS "esp_components"): Устанавливает переменную EXTRA_COMPONENT_DIRS в значение "esp_components". Это, указывает на дополнительную директорию, где CMake должен искать специфичные для компоненты проекта.

set(TEST_COMPONENTS "sim900d_uart" CACHE STRING "List of components to test") позволяет указать, какие именно компоненты участвуют в сборке тестового приложения. Это особенно важно, если вы не хотите включать весь проект или если нужно протестировать только подмножество компонентов.

include($ENV{IDF_PATH}/tools/cmake/project.cmake): Включает скрипт project.cmake из директории установки ESP-IDF (путь к которой берется из переменной окружения IDF_PATH). Этот скрипт содержит логику для сборки проектов на базе ESP-IDF.

project(unit_test_test): Объявляет имя проекта CMake как unit_test_test.

В test_main/main/CMakeLists.txt необходимо перечислить файлы .c для компиляции.
Файлы самих unit-тестов в компонентах указываются в папках компонентов.