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
            "network/wifi_manager.cpp"

        # 2. 对外暴露的头文件搜索目录
        INCLUDE_DIRS
            "."
            "config"
            "core"
            "comm"
            "network"

        # 3. 依赖的 ESP-IDF 底层组件
        REQUIRES
            driver           # 基础外设驱动
            esp_driver_uart  # UART 独立驱动
            freertos         # FreeRTOS 核心
            log              # 日志打印
            esp_system       # 系统基础信息
            esp_psram        # 8MB PSRAM 内存管理
            spi_flash        # Flash 查询
            nvs_flash        # NVS 存储 (Wi-Fi 必须)
            esp_wifi         # Wi-Fi 硬件驱动
            esp_event        # 系统事件循环
            esp_netif        # TCP/IP 协议栈
    )
