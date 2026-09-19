cmake_minimum_required(VERSION 3.16)

# 交叉编译工具链定义
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)

project(firmware_chassis C CXX ASM)

set(CMAKE_C_STANDARD 11)

# Cortex-M4F 硬件浮点编译参数 (单精度浮点运算硬件加速)
set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS "${CPU_FLAGS} -Wall -Wextra -fdata-sections -ffunction-sections -O2 -g")
set(CMAKE_ASM_FLAGS "${CPU_FLAGS} -x assembler-with-cpp")

# 链接器参数
set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/STM32G473RCTx_FLASH.ld")
set(CMAKE_EXE_LINKER_FLAGS "${CPU_FLAGS} -T${LINKER_SCRIPT} --specs=nano.specs --specs=nosys.specs -u _printf_float -Wl,-Map=${PROJECT_NAME}.map,--cref -Wl,--gc-sections")

# 宏定义
add_definitions(
        -DSTM32G473xx
    )

# 头文件包含路径
include_directories(
        Inc
    )

# 源文件列表 (包含我们手写的所有模块)
set(SOURCES
        Src/main.c
        Src/bsp.c
        Src/bsp_spi.c
        Src/icm42605.c
        Src/system_stm32g4xx.c
        Src/startup_stm32g473xx.s
        Src/bsp_usart.c
    )

add_executable(${PROJECT_NAME}.elf ${SOURCES})

# 编译后自动提取生成 .bin 和 .hex 文件，并打印内存占用统计
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
        COMMAND ${CMAKE_OBJCOPY} -O ihex ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
        COMMAND ${CMAKE_SIZE} --format=berkeley ${PROJECT_NAME}.elf
        COMMENT "Building binary and printing size..."
    )
