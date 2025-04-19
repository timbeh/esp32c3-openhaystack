cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32c3-openhaystack)

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    $ENV{IDF_PATH}/components/bt/host/nimble/nimble/nimble/include
    $ENV{IDF_PATH}/components/bt/host/nimble/port/include
    $ENV{IDF_PATH}/components/bt/host/nimble/nimble/porting/nimble/include
    $ENV{IDF_PATH}/components/bt/host/nimble/nimble/porting/npl/freertos/include
)
