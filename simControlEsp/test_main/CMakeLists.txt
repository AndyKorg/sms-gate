# This is the project CMakeLists.txt file for the test subproject
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS 
    "../esp_components"
)
include_directories("../esp_components/sim900d_uart/test/mocks")  # <-- важный момент
set(TEST_COMPONENTS "sim900d_uart" CACHE STRING "List of components to test")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(unit_test_test)
