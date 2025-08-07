# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)
set(EXTRA_COMPONENT_DIRS "esp_components")

# include($ENV{ADF_PATH}/CMakeLists.txt)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(my_project_name "simControlEsp")
set(DISPLAY_NAME SimC)
set(SOUND_NAME non)

#create version name string
file(READ sdkconfig conf)
string(FIND ${conf} "CONFIG_CIRCUIT_V1=y" CONFIG_CIRCUIT_V1)


#analyse CONFIG_SOFTWARE_VERSION="13"
set(software_name "CONFIG_SOFTWARE_VERSION=")
string(LENGTH ${software_name} software_name_len)
string(FIND ${conf} ${software_name} software_name_start)
if(NOT (${software_name_start} EQUAL "-1)"))
	math(EXPR software_num_start "${software_name_start}+${software_name_len}") 
	#10 - number of version number characters with reserve
	string(SUBSTRING ${conf} ${software_num_start} 10 software_var)
	string(REGEX MATCH "[0-9][0-9]?[0-9]?" software_version ${software_var})
endif()
if(NOT(${software_version} MATCHES "[0-9][0-9]?[0-9]?")	)
	message(FATAL_ERROR "ERROR: Software version not set, a number from 1 to 999 is required! (see. Watch matrix menu sdkconfig)")
endif()
# message(STATUS "CONFIG_SOFTWARE_VERSION:" ${software_version})

set(HW_VER_NAME no_set)
if(NOT (${CONFIG_CIRCUIT_V1} EQUAL "-1)"))
	set(HW_VER_NAME 1)
endif()

set(NAME_FILE_OTA "${my_project_name}@${DISPLAY_NAME}_${SOUND_NAME}@${HW_VER_NAME}@${software_version}.bin")
file(WRITE version.txt ${DISPLAY_NAME}_${SOUND_NAME}@${HW_VER_NAME}@${software_version})

project(${my_project_name})

idf_build_get_property(build_dir BUILD_DIR)
idf_build_get_property(elf_name EXECUTABLE_NAME GENERATOR_EXPRESSION)
set(app_bin "${build_dir}/${elf_name}.bin")

add_custom_command(OUTPUT /tmp/app.bin
#  DEPENDS "${build_dir}/${app_bin}"
  COMMAND "${CMAKE_COMMAND}" -E remove -f /tmp/${NAME_FILE_OTA}
  COMMAND "${CMAKE_COMMAND}" -E copy "${app_bin}" /tmp/${NAME_FILE_OTA}
  COMMAND ${CMAKE_COMMAND} -E echo "----"
  COMMAND ${CMAKE_COMMAND} -E echo "!!!COPING binary to /tmp/${NAME_FILE_OTA}"
  COMMAND ${CMAKE_COMMAND} -E echo "----"
  )

add_custom_target(copy_app_binary ALL DEPENDS /tmp/app.bin)
add_dependencies(copy_app_binary gen_project_binary)


