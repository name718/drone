# ==============================================================================
# @file main/CMakeLists.txt
# @brief 大脑主组件 (main component) 构建规则定义
# ==============================================================================

idf_component_register(
    # 1. 参与编译的 C++ 源码文件列表
    SRCS
        "main.cpp"
        "core/robot_brain.cpp"
        "comm/uart_comm.cpp"
        "comm/chassis_service.cpp"

    # 2. 对外暴露的头文件搜索目录 (支持绝对或相对包含)
    INCLUDE_DIRS
        "."
        "config"
        "core"
        "comm"

    # 3. 依赖的 ESP-IDF 官方底层组件
    REQUIRES
        driver           # 传统外设基础驱动
        esp_driver_uart  # ESP-IDF 5.x/6.x UART 独立驱动组件
        freertos         # FreeRTOS 操作系统核心
        log              # ESP 日志打印组件
        esp_system       # 系统基础信息与芯片属性
        esp_psram        # 8MB Octal PSRAM 内存管理
        spi_flash        # 16MB Flash 容量查询与操作
)
