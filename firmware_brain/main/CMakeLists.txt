idf_component_register(
    SRCS
        "main.cpp"
        "display/oled_driver.cpp"
    INCLUDE_DIRS
        "."
        "display"
    REQUIRES
        driver          # 通用外设驱动基类
        esp_driver_i2c  # 硬件 I2C Master 驱动库 (用于 OLED 通信)
        esp_timer       # 高精度硬件定时器
        freertos        # FreeRTOS 实时多任务系统
        log             # ESP_LOG 日志输出库
        esp_psram       # 8MB 外置 PSRAM 内存支持
        esp_system      # 系统基础 API (复位、堆栈查询等)
        spi_flash       # Flash 存储器控制库
)
