cmake_minimum_required(VERSION 3.16)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
project(firmware_head C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(firmware_head.elf
 Src/main.cpp Src/board.cpp Src/head_display.cpp
 ../shared/ui/canvas.cpp ../shared/ui/face_engine.cpp ../shared/ui/head_controller.cpp
 vendor/cmsis-device-f1/startup_stm32f103xb.s)
target_include_directories(firmware_head.elf PRIVATE Inc ../shared ../protocol vendor/cmsis-device-f1 vendor/cmsis-core)
target_compile_definitions(firmware_head.elf PRIVATE STM32F103xB)
target_compile_options(firmware_head.elf PRIVATE
 -mcpu=cortex-m3 -mthumb -Os -g3 -Wall -Wextra -Werror -ffunction-sections -fdata-sections
 $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti;-fno-use-cxa-atexit;-fno-threadsafe-statics>)
target_link_options(firmware_head.elf PRIVATE
 -mcpu=cortex-m3 -mthumb -nostartfiles --specs=nano.specs --specs=nosys.specs
 "-T${CMAKE_CURRENT_SOURCE_DIR}/STM32F103C8_FLASH.ld"
 "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/firmware_head.map,--gc-sections,--print-memory-usage")
target_link_libraries(firmware_head.elf PRIVATE m)
set_property(TARGET firmware_head.elf APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/STM32F103C8_FLASH.ld")
add_custom_command(TARGET firmware_head.elf POST_BUILD
 COMMAND arm-none-eabi-objcopy -O binary firmware_head.elf firmware_head.bin
 COMMAND arm-none-eabi-objcopy -O ihex firmware_head.elf firmware_head.hex
 COMMAND arm-none-eabi-size firmware_head.elf)
