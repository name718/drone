# 📦 硬件实测验证归档固件包 (Hardware Test Binaries)

> 本目录存放经过实物联调 100% 验证通过的完整硬件测试二进制文件。无论未来代码如何重构，随时可以直接通过本目录脚本一键刷入，快速排查硬件故障。

---

## 1. 🏎️ STM32G473 底盘自检固件 (`stm32/`)
- **包含文件**：
  - `firmware_chassis.bin` / `firmware_chassis.hex`：含 TB6612 电机驱动、TIM1/TIM2 硬件正交编码器闭环、ICM-42605 姿态解算。
- **一键烧录**：
  ```bash
  ./binaries_hardware_test/stm32/flash_stm32.sh
  ```

---

## 2. 🧠 ESP32-S3 大脑全项自检固件 (`esp32/`)
- **包含文件**：
  - `bootloader.bin` (0x0)
  - `partition-table.bin` (0x8000)
  - `firmware_brain.bin` (0x10000)
- **自检功能**：
  - 1.8寸 TFT 彩屏三原色循环 + 赛博大眼动态表情 + 底部音量 VU 电平表
  - 双轴云台舵机平滑巡航
  - 开机自动播放《超级马里奥》过关旋律 (喇叭全频段自检)
  - INMP441 麦克风 16kHz 实时采集，拍手声波检测与萌宠互动回应
  - Wi-Fi 自动联网
- **一键烧录**：
  ```bash
  ./binaries_hardware_test/esp32/flash_esp32.sh
  ```
