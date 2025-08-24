# This is the project CMakeLists.txt file for the test subproject
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "../esp_components")
set(TEST_COMPONENTS 
    "sim900d_uart" 
    "version" 
    CACHE STRING 
    "List of components to test")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(unit_test_test)
