# ==============================================================================
# @file main/CMakeLists.txt
# @brief 大脑主组件 (main component) 构建规则与硬件依赖定义
#
# 【语法说明】：
#  idf_component_register 是 ESP-IDF 构建系统的核心宏命令：
#  - SRCS:         登记参与编译的所有 C++ 源文件 (.cpp)
#  - INCLUDE_DIRS: 登记对外暴露的头文件搜索目录 (代码里 #include 可以直接引用)
#  - REQUIRES:     登记本组件依赖的 ESP-IDF 官方底层系统级与硬件驱动组件
# ==============================================================================

idf_component_register(
        SRCS
            "main.cpp"			 # 系统主入口 (负责点火启动)
            "core/robot_brain.cpp"       # 大脑中枢 (单例外观模式，总管全局调度)
            "comm/uart_comm.cpp"         # 串口底层通信实现 (硬件 UART 收发)
            "comm/chassis_service.cpp"   # 底盘通信服务 (跨芯片高速协议交互)
            "network/wifi_manager.cpp"   # Wi-Fi 管理器 (自动连网、断线重连)
            "network/web_server.cpp"     # Web 管理后台与 WebSocket 全双工服务
            "display/st7735.cpp"         # 1.8寸 TFT 彩屏底层硬件驱动
            "display/face_engine.cpp"
            "display/display_service.cpp"
            "gimbal/servo_driver.cpp"    # 硬件 PWM 舵机驱动
            "gimbal/gimbal_service.cpp"  # 二自由度云台运动服务
            "audio/speaker_driver.cpp"   # MAX98357A I2S 功放驱动
            "audio/mic_driver.cpp"       # INMP441 I2S 麦克风驱动
            "audio/audio_service.cpp"    # 音频声学业务服务
            "core/interaction_service.cpp" # 多模态拟人自主交互行为引擎

        INCLUDE_DIRS
            "."                          # 当前 main 根目录
            "config"			 # board_config.hpp (所有引脚唯一信任源)
            "core"			 # robot_brain.hpp
            "comm"			 # 通信相关头文件 (包含共享协议头文件)
            "network"			 # 网络、Wi-Fi 与 Web 模块头文件
            "display"			 # 1.8寸彩屏硬件驱动头文件
            "gimbal"			 # 舵机与头部云台头文件
            "audio"			 # 音频与声学头文件

        REQUIRES
            driver			 # 传统外设驱动兼容基础库
            esp_driver_gpio		 # 控制屏幕 RST/DC/BLK 引脚高低电平
            esp_driver_uart		 # 控制串口芯片通信 (GPIO 7/8 发送底盘指令)
            esp_driver_spi		 # 控制 SPI2 高速硬件总线 (GPIO 11/12/14 刷屏推流)
            esp_driver_ledc		 # 控制硬件 LEDC PWM 输出 (GPIO 1/2 控制 SG90 舵机)
            esp_driver_i2s		 # 控制硬件 I2S 音频接口 (I2S0 麦克风 / I2S1 功放)
            freertos			 # FreeRTOS 实时多任务操作系统内核 (任务调度/队列)
            log                          # 提供 ESP_LOGI / ESP_LOGE 等彩色串口日志输出
            esp_system			 # 提供系统启动、芯片重启、硬件自检信息接口
            esp_psram			 # 提供板载 8MB Octal PSRAM 扩展大内存管理支持
            spi_flash			 # 提供板载 16MB 高速 Flash 存储查询与管理
            nvs_flash			 # 提供非易失性存储库 (Wi-Fi 底层射频物理校准必须依赖)
            esp_wifi			 # 提供 ESP32-S3 原生 2.4GHz Wi-Fi 硬件射频驱动
            esp_event			 # 提供系统级事件循环通知机制 (监听 Wi-Fi 连断状态)
            esp_netif			 # 提供 TCP/IP 协议栈网络接口适配层 (获取本机 IP)
            esp_http_server		 # 提供高性能嵌入式 Web 服务器与 WebSocket 全双工协议
            esp_timer			 # 提供高精度硬件微秒定时器 (计算系统开机运行时间 Uptime)
    )
