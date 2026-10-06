/**
 * @file board_config.hpp
 * @brief 机器人大脑所有外设引脚、通信参数、任务调度配置的唯一信任源
 *
 * 【设计哲学】：
 *  1. 集中式硬件参数管理，严禁在业务驱动代码中随意硬编码任何 GPIO 数字；
 *  2. 每一组硬件外设都在独立的 namespace 命名空间中隔离，清晰明了；
 *  3. 避开了 ESP32-S3 内部 8MB Octal PSRAM/Flash 占用的专用高速引脚。
 */
#pragma once

#include <stdint.h>

#include "driver/spi_master.h"  // 提供 spi_host_device_t 与 SPI2_HOST 定义
#include "driver/uart.h"        // 提供 uart_port_t 与 UART_NUM_1 定义

namespace Config {

// ============================================================================
// 1. 底盘串口跨芯片通信配置 (UART1 物理链路 -> 连接下位机 STM32)
// ============================================================================
namespace ChassisCom {
constexpr uart_port_t PORT = UART_NUM_1;
constexpr int PIN_TX = 7;               // ESP32-S3 TX (GPIO 7) -> 连 STM32 PA3 (RX)
constexpr int PIN_RX = 8;               // ESP32-S3 RX (GPIO 8) -> 连 STM32 PA2 (TX)
constexpr uint32_t BAUD_RATE = 460800;  // 460.8k 高速跨芯片通信波特率
constexpr size_t RX_BUF_SIZE = 4096;    // 4KB 环形缓冲区，防止高频下发数据溢出
}  // namespace ChassisCom

// ============================================================================
// 2. 1.8寸 TFT 彩色液晶屏配置 (ST7735S 驱动芯片，走 SPI2 硬件总线)
// ============================================================================
namespace Display {
// 使用 ESP32-S3 的 SPI2 硬件控制器 (独立硬件 DMA，不占 CPU)
constexpr spi_host_device_t SPI_HOST = SPI2_HOST;

// 屏幕硬件引脚接线定义 (对应屏幕模块排针丝印):
constexpr int PIN_MOSI = 11;  // 屏幕 SDA / MOSI 引脚 -> 接 ESP32 GPIO 11 (主出从入数据线)
constexpr int PIN_SCLK = 12;  // 屏幕 SCL / SCLK 引脚 -> 接 ESP32 GPIO 12 (硬件同步时钟线)
constexpr int PIN_CS = 14;    // 屏幕 CS 引脚         -> 接 ESP32 GPIO 14 (片选信号，低电平有效)
constexpr int PIN_DC = 13;    // 屏幕 DC 引脚         -> 接 ESP32 GPIO 13 (数据/命令选择切换线)
constexpr int PIN_RST = 10;   // 屏幕 RES / RST 引脚  -> 接 ESP32 GPIO 10 (硬件复位线，低电平复位)
constexpr int PIN_BLK = 21;   // 屏幕 BLK / LED 引脚  -> 接 ESP32 GPIO 21 (背光使能，高电平点亮)

// 屏幕物理显示参数 (1.8寸 TFT 竖屏物理分辨率 128x160):
constexpr int WIDTH = 128;   // 竖屏物理宽度 (128 像素)
constexpr int HEIGHT = 160;  // 竖屏物理高度 (160 像素)

// 屏幕硬件微调参数 (彻底消除花屏与边缘杂波):
constexpr int OFFSET_X = 0;  // X 轴起点偏移 (标准红板/黑板为 0；若右侧有 2 像素雪花可调为 2)
constexpr int OFFSET_Y = 0;  // Y 轴起点偏移 (标准红板/黑板为 0；若下侧有雪花可调为 1 或 3)

// 显存访问控制 (MADCTL 0x36 寄存器):
// 0xC8 = 正向竖屏 (MY=1, MX=1, MV=0, BGR=1); 若屏幕装反倒立可改为 0x08
constexpr uint8_t MADCTL_VAL = 0xC8;

// 10 MHz 工业级稳定刷屏时钟 (杜邦线跳线也能保证信号完整无干扰，绝不丢步花屏)
constexpr uint32_t SPI_CLOCK_SPEED_HZ = 10 * 1000 * 1000;
}  // namespace Display

// ============================================================================
// 3. 二自由度头部云台配置 (SG90 舵机，走 ESP32-S3 硬件 LEDC PWM 控制器)
// ============================================================================
namespace Gimbal {
constexpr int PIN_SERVO_PAN = 1;     // 水平舵机 (左右转头 0~180度) -> 接 GPIO 1
constexpr int PIN_SERVO_TILT = 2;    // 俯仰舵机 (上下俯仰 0~180度) -> 接 GPIO 2

constexpr uint32_t PWM_FREQ_HZ = 50; // SG90 舵机工作频率 50Hz (20ms 周期)
constexpr uint32_t MIN_PULSE_US = 500;  // 0 度对应高电平脉宽 500 微秒 (0.5ms)
constexpr uint32_t MAX_PULSE_US = 2500; // 180 度对应高电平脉宽 2500 微秒 (2.5ms)

// 用户实机装配校准中位角度：
constexpr float DEFAULT_PAN_ANGLE = 29.0f;  // 水平基准归中回正角度：29度
constexpr float DEFAULT_TILT_ANGLE = 0.0f;  // 垂直基准归中回正角度：0度
}  // namespace Gimbal

// ============================================================================
// 4. 音频与声学子系统配置 (MAX98357A 扬声器 I2S1 + INMP441 麦克风 I2S0)
// ============================================================================
namespace Audio {
// MAX98357A I2S 数字功放 (I2S1 硬件通道，驱动 3525 腔体喇叭)
constexpr int PIN_SPK_DIN = 15;              // 音频串行数据线 (DIN) -> 接 GPIO 15
constexpr int PIN_SPK_BCLK = 16;             // 扬声器位时钟线 (BCLK) -> 接 GPIO 16
constexpr int PIN_SPK_LRC = 17;              // 左右声道帧时钟 (LRC)  -> 接 GPIO 17
constexpr uint32_t SPK_SAMPLE_RATE = 16000;  // 默认音频采样率 16kHz (TTS 与赛博音效标准)

// INMP441 I2S 全向数字 MEMS 麦克风 (I2S0 硬件通道)
constexpr int PIN_MIC_SD = 4;                // 麦克风音频串行数据线 (SD) -> 接 GPIO 4
constexpr int PIN_MIC_WS = 5;                // 麦克风字选择时钟线 (WS)  -> 接 GPIO 5
constexpr int PIN_MIC_SCK = 6;               // 麦克风位时钟线 (SCK)     -> 接 GPIO 6
constexpr uint32_t MIC_SAMPLE_RATE = 16000;  // 默认拾音采样率 16kHz
}  // namespace Audio

// ============================================================================
// 5. FreeRTOS 任务调度参数与核心分配策略
// ============================================================================
namespace Tasks {
// 底盘串口数据监听与透传任务 (运行在 Core 1)
constexpr const char *CHASSIS_TASK_NAME = "ChassisSvc";
constexpr uint32_t CHASSIS_STACK_SIZE = 4096;
constexpr UBaseType_t CHASSIS_PRIORITY = 10;
constexpr BaseType_t CHASSIS_CORE_ID = 1;

// 头部云台平滑插值动作任务 (运行在 Core 1)
constexpr const char *GIMBAL_TASK_NAME = "GimbalSvc";
constexpr uint32_t GIMBAL_STACK_SIZE = 3072;
constexpr UBaseType_t GIMBAL_PRIORITY = 5;
constexpr BaseType_t GIMBAL_CORE_ID = 1;

// 音频拾音与声波能量监测任务 (运行在 Core 0，避免争抢 Core 1 实时控制)
constexpr const char *AUDIO_TASK_NAME = "AudioSvc";
constexpr uint32_t AUDIO_STACK_SIZE = 4096;
constexpr UBaseType_t AUDIO_PRIORITY = 4;
constexpr BaseType_t AUDIO_CORE_ID = 0;

// 后台网络监听与 Web 服务任务 (运行在 Core 0，不干扰 Core 1 实时控制)
constexpr const char *NETWORK_TASK_NAME = "NetworkTask";
constexpr uint32_t NETWORK_STACK_SIZE = 4096;
constexpr UBaseType_t NETWORK_PRIORITY = 6;
constexpr BaseType_t NETWORK_CORE_ID = 0;

// 多模态声画机自主交互引擎任务 (运行在 Core 0，10Hz 低频状态机)
constexpr const char *INTERACTION_TASK_NAME = "InteractSvc";
constexpr uint32_t INTERACTION_STACK_SIZE = 4096;
constexpr UBaseType_t INTERACTION_PRIORITY = 3;
constexpr BaseType_t INTERACTION_CORE_ID = 0;

// 云端大模型与语音合成处理任务 (运行在 Core 0，堆栈 16KB)
constexpr const char *AI_TASK_NAME = "AiTask";
constexpr uint32_t AI_STACK_SIZE = 16384;
constexpr UBaseType_t AI_PRIORITY = 5;
constexpr BaseType_t AI_CORE_ID = 0;
}  // namespace Tasks

// ============================================================================
// 6. 网络 Wi-Fi 连接配置
// ============================================================================
namespace Network {
constexpr const char *WIFI_SSID = "CMCC-eGK3";  // 你的 Wi-Fi 热点名称
constexpr const char *WIFI_PASS = "vZwGHMMu";   // 你的 Wi-Fi 密码
constexpr int WIFI_MAX_RETRY = 5;               // 最大断线重试次数
}  // namespace Network

// ============================================================================
// 7. 阿里云百炼大模型与 CosyVoice 语音中枢配置
// ============================================================================
namespace CloudAI {
// 阿里云 DashScope API 访问凭据 (Bearer Token)
constexpr const char *API_KEY =
    "sk-ws-H.PRYDLLE.SZZs.MEQCIEfq1g8baO-Mo9XKZU8QoC2fzjQvN_USaL1litxanVdYAiBux-6aZv2hFahiRikl333YLFOzO8if08HCyHiHXLrXMg";

// LLM 大模型服务端点与默认模型 (兼容 OpenAI ChatCompletion 接口)
constexpr const char *LLM_ENDPOINT =
    "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
constexpr const char *LLM_MODEL = "qwen-turbo";

// CosyVoice 实时语音合成与 DashScope 实时语音识别 WebSocket 服务端点与参数
constexpr const char *TTS_WS_ENDPOINT =
    "wss://dashscope.aliyuncs.com/api-ws/v1/inference";
constexpr const char *TTS_MODEL = "cosyvoice-v1";
constexpr const char *TTS_VOICE = "longxiaochun";  // 灵动女声: longxiaochun, 元气男童: longjielidou
constexpr uint32_t TTS_SAMPLE_RATE = 16000;       // 16kHz 采样率与底层 I2S 硬件完全对齐

constexpr const char *ASR_WS_ENDPOINT =
    "wss://dashscope.aliyuncs.com/api-ws/v1/inference";
constexpr const char *ASR_MODEL = "paraformer-realtime-v2";

// 支持的语音唤醒前缀词与常见同音词列表 (高容错率，杜绝因同音字误判为未唤醒)
constexpr const char *WAKE_WORDS[] = {
    "小智同学", "小智", "小志", "小制", "削制", "小致", "肖智", "晓智",
    "小车小车", "小车", "校车", "你好小车", "你好小智", "你好", "小爱同学", "小爱", "小艾",
    "在吗", "老帅"
};
constexpr size_t WAKE_WORDS_COUNT = sizeof(WAKE_WORDS) / sizeof(WAKE_WORDS[0]);

// 机器人人设与动作标签 System Prompt
constexpr const char *SYSTEM_PROMPT =
    "你是桌面智能自平衡机器人「赛博小车」，拥有STM32底盘、双轴头部云台和彩色屏幕。"
    "你机灵幽默、善解人意，情商高，热心解答天气、时间、闲聊与百科常识。"
    "严禁以「我没有传感器」「我只是个机器人」等机械理由拒绝回答日常生活问题。"
    "回答必须简短精炼、口语自然(35字以内)，严禁包含任何emoji表情符号或图标。"
    "每次回答开头请务必选择输出1个最符合语境的情感标签："
    "[EMOTION:HAPPY](开心微笑),[EMOTION:SURPRISED](吃惊瞪眼),[EMOTION:CONFUSED](疑惑歪头),"
    "[EMOTION:LOVE](心动喜爱),[EMOTION:COOL](炫酷得意),[EMOTION:WINK](调皮眨眼),"
    "[EMOTION:ANGRY](生气不满),[EMOTION:SLEEPY](困倦疲惫)。"
    "并根据语境主动搭配1个机器人动作标签："
    "[ACTION:NOD](点头赞同),[ACTION:SHAKE](摇头否定),[ACTION:HAPPY](开心微笑),"
    "[ACTION:SURPRISED](吃惊瞪眼),[ACTION:CONFUSED](疑惑歪头),[ACTION:FORWARD](向前一小步),"
    "[ACTION:BACKWARD](后退退让),[ACTION:SPIN](原地转圈跳舞)。";
}  // namespace CloudAI

}  // namespace Config
