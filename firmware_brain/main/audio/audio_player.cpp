// • ESP-IDF 5.x/6.x 放弃了旧版冗余的 API，改用清晰的 Channel（通道）架构：
//     1. i2s_new_channel：向系统申请 I2S 硬件控制器与 DMA（直接内存访问）通道；
//     2. i2s_channel_init_std_mode：配置为标准 Philips I2S 协议模式（16
//     位位宽、16000Hz 采样率、绑定引脚 GPIO 15/16/17）；
//     3. i2s_channel_enable：启动 DMA 传输引擎。

// ### 💡 技术点 2：如何用数学正弦波“无文件合成”清脆科技音？
// • 我们不需要耗费几兆内存去存 MP3/WAV
// 录音文件，单片机可以直接用高中三角函数在内存中“凭空算出一首乐曲”：

//                ⎛2π·f·i⎞
//   y[i] = A·sin ⎜──────⎟
//                ⎝采 样 率 ⎠
// • f：目标声音频率（例如 523 Hz 是中音 C，1046 Hz 是高音 C）；
// • A：音量振幅（在 16 位整型中范围为 0 sim 32767）；
// • 将算出的点阵数据通过 DMA 喂给 MAX98357A，喇叭就能发出纯净无底噪的科技提示音！

/**
 * @file audio_player.cpp
 * @brief MAX98357A I2S 数字音频播放器驱动实现
 */
#include "audio_player.hpp"

#include <cmath>
#include <vector>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AUDIO_PLAYER";

AudioPlayer::AudioPlayer(int bclk_pin, int ws_pin, int dout_pin, uint32_t sample_rate)
    : m_bclkPin(bclk_pin),
      m_wsPin(ws_pin),
      m_doutPin(dout_pin),
      m_sampleRate(sample_rate),
      m_txHandle(nullptr) {}

AudioPlayer::~AudioPlayer() {
    if (m_txHandle) {
        i2s_channel_disable(m_txHandle);
        i2s_del_channel(m_txHandle);
    }
}

esp_err_t AudioPlayer::init() {
    ESP_LOGI(TAG, "初始化 I2S1 音频输出通道 (BCLK: %d, LRC: %d, DIN: %d, 采样率:%" PRIu32 "Hz)...",
             m_bclkPin, m_wsPin, m_doutPin, m_sampleRate);

    // 1. 配置 I2S 控制器与 DMA 通道
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;  // 播放完毕自动将 DMA 缓冲区清零，防止杂音

    esp_err_t ret = i2s_new_channel(&chan_cfg, &m_txHandle, nullptr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S 通道创建失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 配置标准 I2S 协议模式 (16位位宽、左右双声道/单声道、引脚映射)
    i2s_std_slot_config_t slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;  // 关键：每声道32BCLK，全帧64 BCLK
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(m_sampleRate),
        .slot_cfg = slot_cfg,
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = static_cast<gpio_num_t>(m_bclkPin),
                .ws = static_cast<gpio_num_t>(m_wsPin),
                .dout = static_cast<gpio_num_t>(m_doutPin),
                .din = I2S_GPIO_UNUSED,
                .invert_flags =
                    {
                        .mclk_inv = false,
                        .bclk_inv = false,
                        .ws_inv = false,
                    },
            },
    };

    ret = i2s_channel_init_std_mode(m_txHandle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S 标准模式初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. 启用 I2S DMA 发送通道
    ret = i2s_channel_enable(m_txHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S 通道启用失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "✅ I2S 音频播放器 (MAX98357A) 初始化成功！");
    return ESP_OK;
}

esp_err_t AudioPlayer::write(const void *data, size_t size_bytes) {
    if (!m_txHandle || !data || size_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t bytes_written = 0;
    return i2s_channel_write(m_txHandle, data, size_bytes, &bytes_written, portMAX_DELAY);
}

void AudioPlayer::playTone(float freq_hz, uint32_t duration_ms, float volume) {
    if (!m_txHandle || freq_hz <= 0)
        return;

    // 限制音量在 0.0 ~ 1.0
    if (volume < 0.0f)
        volume = 0.0f;
    if (volume > 1.0f)
        volume = 1.0f;

    // 计算总采样点数
    size_t total_samples = (m_sampleRate * duration_ms) / 1000;
    // 双声道立体声: 每个采样点占用 2 个声道 * 2 字节(16-bit) = 4 字节
    std::vector<int16_t> pcm_buffer(total_samples * 2);
    float max_amplitude = 30000.0f * volume;  // 预留余量，防止削顶破音
    float phase_step = (2.0f * M_PI * freq_hz) / m_sampleRate;
    float current_phase = 0.0f;

    // 平滑渐入渐出点数 (各占 10%，消除开关音与爆破音)
    size_t fade_len = total_samples / 10;
    if (fade_len == 0)
        fade_len = 1;

    for (size_t i = 0; i < total_samples; i++) {
        // 计算渐入渐出包络系数 (0.0 ~ 1.0)
        float envelope = 1.0f;
        if (i < fade_len) {
            envelope = static_cast<float>(i) / fade_len;  // 开头平滑渐入
        } else if (i > total_samples - fade_len) {
            envelope = static_cast<float>(total_samples - i) / fade_len;  // 结尾平滑渐出
        }

        // 利用正弦波与包络相乘，生成平滑 PCM 幅值
        int16_t sample_val = static_cast<int16_t>(max_amplitude * envelope * sinf(current_phase));

        pcm_buffer[i * 2] = sample_val;      // 左声道
        pcm_buffer[i * 2 + 1] = sample_val;  // 右声道

        current_phase += phase_step;
        if (current_phase >= 2.0f * M_PI) {
            current_phase -= 2.0f * M_PI;
        }
    }

    // 通过 I2S DMA 发送给功放播放
    write(pcm_buffer.data(), pcm_buffer.size() * sizeof(int16_t));

    // 尾部补充一小段静音缓冲，确保 DMA 彻底平稳输出
    int16_t silence[64] = {0};
    write(silence, sizeof(silence));

    // float max_amplitude = 32767.0f * volume;
    // float phase_step = (2.0f * M_PI * freq_hz) / m_sampleRate;
    // float current_phase = 0.0f;

    // for (size_t i = 0; i < total_samples; i++) {
    //     // 利用正弦波公式生成 16-bit PCM 幅值
    //     int16_t sample_val = static_cast<int16_t>(max_amplitude * sinf(current_phase));

    //     // 左右声道写入相同采样值
    //     pcm_buffer[i * 2] = sample_val;      // 左声道
    //     pcm_buffer[i * 2 + 1] = sample_val;  // 右声道

    //     current_phase += phase_step;
    //     if (current_phase >= 2.0f * M_PI) {
    //         current_phase -= 2.0f * M_PI;
    //     }
    // }

    // // 通过 I2S DMA 发送给功放播放
    // write(pcm_buffer.data(), pcm_buffer.size() * sizeof(int16_t));
}

void AudioPlayer::playBootSound() {
    ESP_LOGI(TAG, "🔊 播放开机科技和弦音...");

    // 经典未来科技感上升和弦 (C5 -> E5 -> G5 -> C6)
    playTone(523.25f, 80, 0.4f);    // C5 (哆)
    playTone(659.25f, 80, 0.4f);    // E5 (咪)
    playTone(783.99f, 80, 0.4f);    // G5 (索)
    playTone(1046.50f, 150, 0.5f);  // C6 (高音哆)
}
