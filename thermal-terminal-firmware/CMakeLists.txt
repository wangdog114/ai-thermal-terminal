cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include($ENV{IDF_PATH}/components/spiffs/project_include.cmake)
project(thermal_terminal)
spiffs_create_partition_image(assets assets FLASH_IN_PROJECT)
