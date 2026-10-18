# 官方依赖来源

本目录只保留构建 STM32F103C8T6 所需的 CMSIS 文件，未修改供应商代码。

- ST CMSIS Device F1：`v4.3.5`，提交 `8a76309ed1250d817e9c888c4417171d2ba3ba63`，来源 https://github.com/STMicroelectronics/cmsis-device-f1 。
- ARM CMSIS Core：`5.9.0`，提交 `61e36449f53c25ef7825c40f7dd93685736f457f`，来源 https://github.com/ARM-software/CMSIS_5 。

原许可证随源码保留。`stm32f103xb.h` 覆盖中容量F103系列；实际可用Flash由项目链接脚本严格限定为64KB，不因为文件名中的`xB`而使用128KB。
