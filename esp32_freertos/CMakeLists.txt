cmake_minimum_required(VERSION 3.16)

# 指定实物芯片型号。ESP-IDF 会为 ESP32-S3 选择对应的工具链和 FreeRTOS 移植层。
set(IDF_TARGET "esp32s3")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_freertos)
