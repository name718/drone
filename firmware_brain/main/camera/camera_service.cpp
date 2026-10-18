#include "camera/camera_service.hpp"

#include <cstdio>
#include <cstring>
#include <atomic>
#include <cstdlib>

#include "comm/head_link.hpp"
#include "config/board_config.hpp"
#include "driver/i2c_master.h"
#include "esp_camera.h"
#include "esp_camera_af.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#if !CONFIG_ROBOT_HEAD_REMOTE
#error "OV5640 wiring requires remote display/servos; legacy GPIO outputs conflict with DVP"
#endif

namespace {
constexpr size_t JPEG_CAPACITY = 64 * 1024;
constexpr const char *TAG = "OV5640";
constexpr bool validPins() {
    constexpr int camera[] = {Config::Camera::D0, Config::Camera::D1, Config::Camera::D2,
        Config::Camera::D3, Config::Camera::D4, Config::Camera::D5, Config::Camera::D6,
        Config::Camera::D7, Config::Camera::PCLK, Config::Camera::VSYNC,
        Config::Camera::HREF, Config::Camera::SDA, Config::Camera::SCL};
    constexpr int occupied[] = {Config::Audio::PIN_MIC_SD, Config::Audio::PIN_MIC_WS,
        Config::Audio::PIN_MIC_SCK, Config::Audio::PIN_SPK_DIN, Config::Audio::PIN_SPK_BCLK,
        Config::Audio::PIN_SPK_LRC, Config::HeadCom::PIN_TX, Config::HeadCom::PIN_RX,
        Config::ChassisCom::PIN_TX, Config::ChassisCom::PIN_RX};
    for (unsigned i = 0; i < sizeof(camera) / sizeof(camera[0]); ++i) {
        if (camera[i] < 0 || camera[i] > 48 || (camera[i] >= 26 && camera[i] <= 37)) return false;
        for (unsigned j = 0; j < i; ++j)
            if (camera[i] == camera[j]) return false;
        for (int pin : occupied)
            if (camera[i] == pin) return false;
    }
    return true;
}
static_assert(validPins(), "Camera pins overlap audio/UART, PSRAM/Flash, or each other");
portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
struct Stats {
    const char *stage = "not_started";
    esp_err_t error = ESP_OK;
    uint32_t frames = 0, failures = 0, bytes = 0, capture_ms = 0;
    uint32_t last_frame_ms = 0;
    uint16_t sensor_id = 0;
    bool af_ready = false;
    int profile = -1;
    int af_raw = -1, af_ack = -1;
    unsigned width = 320, height = 240;
    int color_mode = -1, awb_r = -1, awb_g = -1, awb_b = -1;
    bool focused = false;
} stats;
SemaphoreHandle_t image_lock = nullptr;
std::atomic<bool> worker_running{false};
std::atomic<int> requested_profile{17};  // 默认VGA、DVP约3MHz；0x3906保持模组原值以保证色彩。
std::atomic<bool> focus_requested{false};
std::atomic<int> requested_color{0};
uint8_t *latest = nullptr, *http_image = nullptr;
size_t latest_size = 0;
uint32_t latest_sequence = 0;
int64_t latest_at_us = 0;
httpd_handle_t image_server = nullptr;
constexpr uint16_t color_regs[] = {0x4300,0x501f,0x5000,0x5001,0x5003,0x503d,0x5381,0x5382,0x5383,0x5384,0x5385,0x5386,0x5387,0x5388,0x5389,0x538a,0x538b,0x5580,0x5583,0x5584,0x5586,0x5587,0x5588,0x3820,0x3821,0x4514,0x3621,0x3906,0x3901,0x3406};
int color_reg_values[sizeof(color_regs)/sizeof(color_regs[0])] = {};

constexpr uint16_t analog[][2] = {{0x3630,0x36},{0x3631,0x0e},{0x3632,0xe2},
        {0x3633,0x12},{0x3621,0xe0},{0x3704,0xa0},{0x3703,0x5a},{0x3715,0x78},
        {0x3717,0x01},{0x370b,0x60},{0x3705,0x1a},{0x3905,0x02},
        {0x3901,0x0a},{0x3731,0x12},{0x3620,0x52},{0x371b,0x20},{0x3635,0x13},
        {0x3636,0x03},{0x3622,0x01},{0x4005,0x1a}};
int original[sizeof(analog)/sizeof(analog[0])] = {};

esp_err_t timingProfile(int profile) {
    sensor_t *sensor = esp_camera_sensor_get();
    if (!sensor || sensor->id.PID != OV5640_PID || !sensor->set_pll)
        return ESP_ERR_NOT_SUPPORTED;
    // 先恢复所选尺寸的官方配置。公开set_pll的参数是root在前、pre在后，
    // 与驱动内部同名helper的顺序不同，不能直接照抄内部调用或旧例程。
    int result = sensor->set_framesize(sensor, profile >= 10 && profile != 14 ? FRAMESIZE_VGA : FRAMESIZE_QVGA);
    if (!result) result = sensor->set_colorbar(sensor, profile == 15 || profile == 18 || profile == 20);
    // 对照Linux官方OV5640模拟前端初始化值。esp32-camera的精简表未显式设置这些寄存器。
    // 数据参考：https://github.com/torvalds/linux/blob/master/drivers/media/i2c/ov5640.c
    // 此模组不覆盖0x3906；复制其0x10值会破坏颜色，已用八色彩条逐项验证。
    static bool saved = false;
    if (!saved) {
        for (unsigned i=0;i<sizeof(analog)/sizeof(analog[0]);++i) original[i]=sensor->get_reg(sensor,analog[i][0],0xff);
        saved = true;
    }
    for (unsigned i=0; !result && i<sizeof(analog)/sizeof(analog[0]);++i) {
        if (original[i] < 0) result = -1;
        else result = sensor->set_reg(sensor,analog[i][0],0xff,profile>=16?analog[i][1]:original[i]);
    }
    if (!result) result = sensor->set_gain_ctrl(sensor, profile == 16 ? 0 : 1);
    if (!result && profile == 16) result = sensor->set_agc_gain(sensor, 0);
    if (!result && profile >= 16) {
        result = sensor->set_reg(sensor,0x3c00,0x04,0x04) |
                 sensor->set_reg(sensor,0x3c01,0x80,0x80) |
                 sensor->set_aec2(sensor,0);
    }
    if (!result && profile >= 12) {
        result = sensor->set_pll(sensor, 0, 80, profile == 13 || profile == 14 ? 2 : 4, 1, 2, 2, 1, profile == 13 || profile == 14 || profile >= 19 ? 8 : 4);
    } else if (!result && profile >= 10) {
        result = sensor->set_pll(sensor, 0, 200, 4, 1, profile == 10 ? 2 : 0, 2, 1, profile == 10 ? 8 : 16);
    } else if (!result && profile == 7) {
        result = sensor->set_pll(sensor, 0, 200, 4, 1, 0, 2, 1, 8);
    } else if (!result && profile >= 8) {
        // 保留3MHz输出时钟，单独比较内部PLL根分频，避免把输出降频等同于内部时序正确。
        result = sensor->set_pll(sensor, 0, 80, 4, 1, 2, 2, 1, profile == 8 ? 4 : 8);
    } else if (!result && profile != 0) {
        int multiplier = (profile == 3 || profile == 4 || profile == 5) ? 80 : 180;
        int divider = profile == 5 ? 24 : 8;
        result = sensor->set_pll(sensor, 0, multiplier, 4, 0, 2, 2, 1, divider);
    }
    int polarity = (profile == 2 || profile == 4) ? 0 : (profile == 6 ? 0x24 : 0x20);
    if (!result) result = sensor->set_reg(sensor, 0x4740, 0x24, polarity);
    ESP_LOGI(TAG, "timing profile=%d result=%d", profile, result);
    return result ? ESP_FAIL : ESP_OK;
}

// 恢复无滤镜、标准色彩矩阵和白平衡；不改PLL、尺寸或DVP边沿。
// 色彩修改仍只在采集任务执行，避免HTTP线程和AF同时读写SCCB。
esp_err_t colorProfile(int mode) {
    sensor_t *sensor = esp_camera_sensor_get();
    if (!sensor) return ESP_ERR_INVALID_STATE;
    int result = sensor->set_special_effect(sensor, 0);
    if (!result) result = sensor->set_reg(sensor,0x5001,0x02,0x02);
    if (!result) result = sensor->set_reg(sensor,0x3621,0xff,0xe0);
    if (!result) result = sensor->set_saturation(sensor, 0);
    if (!result) result = sensor->set_contrast(sensor, 0);
    if (!result) result = sensor->set_brightness(sensor, 0);
    if (!result) result = sensor->set_whitebal(sensor, 1);
    int wb = mode == 1 ? 1 : mode == 2 ? 3 : mode == 3 ? 4 : mode == 4 ? 1 : 0;
    if (!result) result = sensor->set_wb_mode(sensor, wb);
    if (!result && mode == 4) {
        for (int reg = 0x3400; !result && reg <= 0x3404; reg += 2) {
            result = sensor->set_reg(sensor, reg, 0xff, 4);
            if (!result) result = sensor->set_reg(sensor, reg + 1, 0xff, 0);
        }
    }
    // 0x3906保持模组复位原值：实机逐项彩条对照证实覆盖0x10会导致蓝绿偏色。
    if (!result) result = sensor->set_colorbar(sensor, mode == 5);
    // 官方colorbar函数包含滚动条；验色使用静止彩条，避免把滚动线误判为损坏。
    if (!result && mode == 5) result = sensor->set_reg(sensor,0x503d,0xc0,0x80);
    if (!result) result = sensor->set_vflip(sensor, mode == 6 || mode == 8);
    if (!result) result = sensor->set_hmirror(sensor, mode == 7 || mode == 8);
    ESP_LOGI(TAG,"color mode=%d result=%d",mode,result);
    return result ? ESP_FAIL : ESP_OK;
}

bool jpegHeaderValid(const uint8_t *data, size_t size) {
    if (size < 4 || data[0] != 0xff || data[1] != 0xd8 ||
        data[size - 2] != 0xff || data[size - 1] != 0xd9) return false;
    size_t pos = 2;
    while (pos < size) {
        if (data[pos++] != 0xff) return false;
        while (pos < size && data[pos] == 0xff) ++pos;
        if (pos + 2 >= size) return false;
        uint8_t marker = data[pos++];
        size_t length = (size_t(data[pos]) << 8) | data[pos + 1];
        if (length < 2 || length > size - pos) return false;
        if (marker == 0xda) return true;  // SOS前的每个段都须完整；主机另做熵解码验收。
        if (marker == 0xd8 || marker == 0xd9 || marker == 0) return false;
        pos += length;
    }
    return false;
}

void stage(const char *name, esp_err_t error = ESP_OK) {
    portENTER_CRITICAL(&stats_lock);
    stats.stage = name;
    stats.error = error;
    portEXIT_CRITICAL(&stats_lock);
    ESP_LOGI(TAG, "stage=%s error=%s", name, esp_err_to_name(error));
}

// 先验证配置总线上的0x5640，再分配DMA/帧缓冲。仅探测原接线表里的42/47。
esp_err_t probeSensor(uint16_t &id) {
    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = static_cast<gpio_num_t>(Config::Camera::SDA);
    bus_config.scl_io_num = static_cast<gpio_num_t>(Config::Camera::SCL);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t bus = nullptr;
    esp_err_t error = i2c_new_master_bus(&bus_config, &bus);
    if (error != ESP_OK) return error;
    error = i2c_master_probe(bus, 0x3c, 100);
    i2c_master_dev_handle_t dev = nullptr;
    if (error == ESP_OK) {
        i2c_device_config_t config{};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = 0x3c;
        config.scl_speed_hz = 100000;
        error = i2c_master_bus_add_device(bus, &config, &dev);
    }
    uint8_t high = 0, low = 0;
    const uint8_t reg_high[] = {0x30, 0x0a}, reg_low[] = {0x30, 0x0b};
    if (error == ESP_OK)
        error = i2c_master_transmit_receive(dev, reg_high, 2, &high, 1, 100);
    if (error == ESP_OK)
        error = i2c_master_transmit_receive(dev, reg_low, 2, &low, 1, 100);
    if (dev) i2c_master_bus_rm_device(dev);
    i2c_del_master_bus(bus);  // 释放后再交给esp_camera官方SCCB驱动，绝不重复持有总线。
    id = (high << 8) | low;
    if (error == ESP_OK && id != 0x5640) error = ESP_ERR_NOT_SUPPORTED;
    ESP_LOGI(TAG, "SCCB address=0x3c sensor_id=0x%04x result=%s", id, esp_err_to_name(error));
    return error;
}

// 仅图像HTTP线程使用http_image。持锁只复制内存，网络发送不占住采集锁。
size_t snapshot(uint32_t &sequence) {
    if (!image_lock || !http_image || xSemaphoreTake(image_lock, pdMS_TO_TICKS(50)) != pdTRUE)
        return 0;
    size_t size = (esp_timer_get_time() - latest_at_us <= 1000000) ? latest_size : 0;
    if (size) std::memcpy(http_image, latest, size);
    sequence = latest_sequence;
    xSemaphoreGive(image_lock);
    return size;
}
}

esp_err_t CameraService::start() {
    if (worker_running.exchange(true)) return ESP_OK;
    if (!image_lock) image_lock = xSemaphoreCreateMutex();
    if (!image_lock) {
        worker_running.store(false);
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(task, "CameraCapture", 6144, nullptr, 2, nullptr, 1) != pdPASS) {
        vSemaphoreDelete(image_lock);
        image_lock = nullptr;
        worker_running.store(false);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void CameraService::task(void *) {
    stage("waiting_head");
    auto &head = HeadLink::getInstance();
    // ESP重启不等于相机断电。先等C8确认休眠，再重新唤醒，避免继承旧状态。
    head.requestCamera(false);
    unsigned asleep_wait = 0;
    while ((!head.online() || (head.status().flags & HP_CAMERA_READY)) && asleep_wait++ < 50)
        vTaskDelay(pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(100));
    head.requestCamera(true);
    unsigned waited = 0;
    while ((!head.online() || !(head.status().flags & HP_CAMERA_READY)) && waited < 100) {
        vTaskDelay(pdMS_TO_TICKS(100));
        ++waited;
    }
    esp_err_t error = ESP_OK;
    if (!head.online() || !(head.status().flags & HP_CAMERA_READY))
        error = ESP_ERR_TIMEOUT;
    uint16_t id = 0;
    if (error == ESP_OK) {
        stage("probing_sccb");
        error = probeSensor(id);
        portENTER_CRITICAL(&stats_lock);
        stats.sensor_id = id;
        portEXIT_CRITICAL(&stats_lock);
    }
    bool initialized = false;
    int applied_profile = requested_profile.load();
    if (error == ESP_OK) {
        stage("initializing_dma");
        camera_config_t config{};
        config.pin_d0 = Config::Camera::D0; config.pin_d1 = Config::Camera::D1;
        config.pin_d2 = Config::Camera::D2; config.pin_d3 = Config::Camera::D3;
        config.pin_d4 = Config::Camera::D4; config.pin_d5 = Config::Camera::D5;
        config.pin_d6 = Config::Camera::D6; config.pin_d7 = Config::Camera::D7;
        config.pin_pclk = Config::Camera::PCLK;
        config.pin_vsync = Config::Camera::VSYNC;
        config.pin_href = Config::Camera::HREF;
        config.pin_sccb_sda = Config::Camera::SDA;
        config.pin_sccb_scl = Config::Camera::SCL;
        config.pin_xclk = Config::Camera::XCLK;
        config.pin_reset = Config::Camera::RESET;
        config.pin_pwdn = Config::Camera::PWDN;
        config.xclk_freq_hz = Config::Camera::XCLK_HZ;
        config.pixel_format = PIXFORMAT_JPEG;
        config.frame_size = FRAMESIZE_QVGA;  // 先以320x240验证现有杜邦线，再提高分辨率。
        config.jpeg_quality = 20;
        config.fb_count = 1;  // 排除双缓存交替帧的色度异常；采集后立即复制并归还。
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_LATEST;
        config.jpeg_buffer_size = JPEG_CAPACITY;
        error = esp_camera_init(&config);
        initialized = error == ESP_OK;
    }
    if (error == ESP_OK) {
        error = timingProfile(applied_profile);
        if (error == ESP_OK) error = colorProfile(requested_color.load());
    }
    if (error == ESP_OK) {
        latest = static_cast<uint8_t *>(heap_caps_malloc(JPEG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        http_image = static_cast<uint8_t *>(heap_caps_malloc(JPEG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!latest || !http_image) error = ESP_ERR_NO_MEM;
    }
    if (error != ESP_OK) {
        stage("failed", error);
        if (initialized) esp_camera_deinit();
        heap_caps_free(latest); heap_caps_free(http_image);
        latest = http_image = nullptr;
        head.requestCamera(false);
        worker_running.store(false);
        vTaskDelete(nullptr);
        return;
    }
    // 图像服务独立端口和HTTP线程，慢速客户端不会堵塞80端口的控制/WebSocket。
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.server_port = 81;
    server_config.ctrl_port = 32769;
    server_config.stack_size = 6144;
    server_config.max_open_sockets = 3;
    server_config.lru_purge_enable = true;
    server_config.send_wait_timeout = 3;
    error = httpd_start(&image_server, &server_config);
    if (error == ESP_OK) {
        httpd_uri_t capture{};
        capture.uri = "/capture"; capture.method = HTTP_GET; capture.handler = captureHandler;
        error = httpd_register_uri_handler(image_server, &capture);
        httpd_uri_t stream{};
        stream.uri = "/stream"; stream.method = HTTP_GET; stream.handler = streamHandler;
        if (error == ESP_OK) error = httpd_register_uri_handler(image_server, &stream);
    }
    if (error != ESP_OK) {
        stage("http_failed", error);
        if (image_server) httpd_stop(image_server);
        esp_camera_deinit();
        heap_caps_free(latest); heap_caps_free(http_image);
        latest = http_image = nullptr;
        head.requestCamera(false);
        worker_running.store(false);
        vTaskDelete(nullptr);
        return;
    }
#if CONFIG_CAMERA_AF_SUPPORT
    stage("initializing_autofocus");
    sensor_t *af_sensor = esp_camera_sensor_get();
    // 官方封装init会立刻发AUTO，旧代码又发一次AUTO，且底层只等2秒。
    // 这里只装载官方AF固件；命令ACK和对焦状态在采集循环中异步观察。
    esp_err_t af_error = af_sensor->af_init && af_sensor->af_init(af_sensor, 6000) == 0 ? ESP_OK : ESP_FAIL;
    if (af_error == ESP_OK) {
        if (af_sensor->set_reg(af_sensor, 0x3023, 0xff, 1) ||
            af_sensor->set_reg(af_sensor, 0x3022, 0xff, 3)) af_error = ESP_FAIL;
    }
    portENTER_CRITICAL(&stats_lock);
    stats.af_ready = af_error == ESP_OK;
    portEXIT_CRITICAL(&stats_lock);
    ESP_LOGI(TAG, "AF firmware/command=%s", esp_err_to_name(af_error));
#endif
    stage("capturing");
    unsigned warmup = 3;
    int64_t last_af_poll = 0;
    int applied_color = requested_color.load();
    portENTER_CRITICAL(&stats_lock);
    stats.color_mode = applied_color;
    portEXIT_CRITICAL(&stats_lock);
    bool focus_releasing = false;
    portENTER_CRITICAL(&stats_lock);
    stats.profile = applied_profile;
    portEXIT_CRITICAL(&stats_lock);
    while (true) {
        int color = requested_color.load();
        if (color != applied_color) {
            error = colorProfile(color);
            if (error == ESP_OK) {
                applied_color = color;
                warmup = 3;
                portENTER_CRITICAL(&stats_lock);
                stats.color_mode = color;
                portEXIT_CRITICAL(&stats_lock);
            }
        }
        int profile = requested_profile.load();
        if (profile != applied_profile) {
            error = timingProfile(profile);  // 只在采集任务操作SCCB，不与HTTP线程并发写寄存器。
            applied_profile = profile;
            warmup = 3;
            xSemaphoreTake(image_lock, portMAX_DELAY);
            latest_size = 0;
            latest_at_us = 0;
            xSemaphoreGive(image_lock);
            portENTER_CRITICAL(&stats_lock);
            stats.profile = applied_profile;
            portEXIT_CRITICAL(&stats_lock);
            stage(error == ESP_OK ? "capturing" : "profile_failed", error);
        }
        if (esp_timer_get_time() - last_af_poll > 500000) {
            sensor_t *sensor = esp_camera_sensor_get();
            int ack = sensor->get_reg(sensor, 0x3023, 0xff);
            int raw = sensor->get_reg(sensor, 0x3029, 0xff);
            int r = sensor->get_reg(sensor, 0x3400, 0xff), rl = sensor->get_reg(sensor, 0x3401, 0xff);
            int g = sensor->get_reg(sensor, 0x3402, 0xff), gl = sensor->get_reg(sensor, 0x3403, 0xff);
            int b = sensor->get_reg(sensor, 0x3404, 0xff), bl = sensor->get_reg(sensor, 0x3405, 0xff);
            if (ack == 0 && focus_releasing) {
                sensor->set_reg(sensor, 0x3023, 0xff, 1);
                sensor->set_reg(sensor, 0x3022, 0xff, 3);
                focus_releasing = false;
            } else if (ack == 0 && focus_requested.exchange(false)) {
                // 先释放上一次AF，再等ACK清零后启动单次搜索，避免在旧任务上叠命令。
                sensor->set_reg(sensor, 0x3023, 0xff, 1);
                sensor->set_reg(sensor, 0x3022, 0xff, 8);
                focus_releasing = true;
            }
            int values[sizeof(color_regs)/sizeof(color_regs[0])];
            for (unsigned i=0;i<sizeof(color_regs)/sizeof(color_regs[0]);++i)
                values[i] = sensor->get_reg(sensor,color_regs[i],0xff);
            portENTER_CRITICAL(&stats_lock);
            std::memcpy(color_reg_values,values,sizeof(values));
            stats.af_ack = ack; stats.af_raw = raw; stats.focused = raw == 0x10;
            stats.awb_r = r < 0 || rl < 0 ? -1 : (r<<8)|rl;
            stats.awb_g = g < 0 || gl < 0 ? -1 : (g<<8)|gl;
            stats.awb_b = b < 0 || bl < 0 ? -1 : (b<<8)|bl;
            portEXIT_CRITICAL(&stats_lock);
            last_af_poll = esp_timer_get_time();
        }
        int64_t started = esp_timer_get_time();
        camera_fb_t *frame = esp_camera_fb_get();
        if (warmup && frame) {
            --warmup;
            esp_camera_fb_return(frame);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        bool valid = frame && frame->format == PIXFORMAT_JPEG && frame->len >= 4 &&
                     frame->len <= JPEG_CAPACITY && jpegHeaderValid(frame->buf, frame->len);
        if (valid && xSemaphoreTake(image_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
            std::memcpy(latest, frame->buf, frame->len);
            latest_size = frame->len;
            latest_at_us = esp_timer_get_time();
            ++latest_sequence;
            xSemaphoreGive(image_lock);
            portENTER_CRITICAL(&stats_lock);
            ++stats.frames;
            stats.bytes = frame->len;
            stats.width = frame->width; stats.height = frame->height;
            stats.capture_ms = (esp_timer_get_time() - started) / 1000;
            stats.last_frame_ms = esp_timer_get_time() / 1000;
            portEXIT_CRITICAL(&stats_lock);
        } else {
            portENTER_CRITICAL(&stats_lock);
            ++stats.failures;
            portEXIT_CRITICAL(&stats_lock);
        }
        if (frame) esp_camera_fb_return(frame);  // 所有分支归还，避免驱动帧缓冲泄漏。
        vTaskDelay(pdMS_TO_TICKS(valid ? 20 : 200));
    }
}

esp_err_t CameraService::statusHandler(httpd_req_t *req) {
    portENTER_CRITICAL(&stats_lock);
    Stats value = stats;
    portEXIT_CRITICAL(&stats_lock);
    char json[768];
    int64_t age = value.frames ? uint32_t(uint32_t(esp_timer_get_time() / 1000) - value.last_frame_ms) : -1LL;
    int size = std::snprintf(json, sizeof(json),
        "{\"stage\":\"%s\",\"error\":\"%s\",\"sensor_id\":%u,\"frames\":%lu,"
        "\"failures\":%lu,\"jpeg_bytes\":%lu,\"capture_ms\":%lu,\"width\":%u,\"height\":%u,"
        "\"color_mode\":%d,\"awb_r\":%d,\"awb_g\":%d,\"awb_b\":%d,\"autofocus_ready\":%s,\"af_raw\":%d,\"af_ack\":%d,\"focused\":%s,\"profile\":%d,\"frame_age_ms\":%lld,\"recent_frame\":%s}",
        value.stage, esp_err_to_name(value.error), value.sensor_id, (unsigned long)value.frames,
        (unsigned long)value.failures, (unsigned long)value.bytes, (unsigned long)value.capture_ms, value.width, value.height,
        value.color_mode, value.awb_r, value.awb_g, value.awb_b, value.af_ready ? "true" : "false", value.af_raw, value.af_ack, value.focused ? "true" : "false", value.profile, (long long)age,
        age >= 0 && age <= 1000 ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, size);
}
esp_err_t CameraService::previewHandler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, "<!doctype html><meta name='viewport' content='width=device-width'><title>机器人摄像头</title>"
        "<body style='background:#17202a;color:white;font:18px sans-serif;text-align:center'><h2>OV5640 实时画面</h2>"
        "<p id='s'>正在连接</p><button onclick=\"fetch('/camera/retry',{method:'POST'}).then(()=>setTimeout(connect,2000))\">重试相机</button>"
        "<button onclick=\"fetch('/camera/focus',{method:'POST'})\">重新对焦</button><p><img id='v' style='width:min(90vw,640px);image-rendering:auto'></p><script>"
        "const v=document.querySelector('#v');function connect(){v.src='http://'+location.hostname+':81/stream?t='+Date.now()}"
        "v.onerror=()=>setTimeout(connect,1500);connect();setInterval(async()=>{try{const r=await fetch('/camera/status');"
        "const s=await r.json();document.querySelector('#s').textContent=s.stage+' · 帧数 '+s.frames+' · 异常 '+s.failures+' · 对焦 '+(s.focused?'完成':(s.autofocus_ready?'未锁定（状态 '+s.af_raw+'）':'未就绪'));}catch(e){}},1000)</script>");
}
esp_err_t CameraService::captureHandler(httpd_req_t *req) {
    uint32_t sequence = 0;
    size_t size = snapshot(sequence);
    if (!size) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Camera frame not ready; see /camera/status on port 80");
    }
    char number[24]; std::snprintf(number, sizeof(number), "%lu", (unsigned long)sequence);
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Frame-Sequence", number);
    return httpd_resp_send(req, reinterpret_cast<const char *>(http_image), size);
}
esp_err_t CameraService::streamHandler(httpd_req_t *req) {
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=robotframe");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    uint32_t previous = 0;
    int64_t last_sent = esp_timer_get_time();
    while (true) {
        uint32_t sequence = 0;
        size_t size = snapshot(sequence);
        if (size && sequence != previous) {
            previous = sequence;
            last_sent = esp_timer_get_time();
            char header[128];
            int n = std::snprintf(header, sizeof(header),
                "\r\n--robotframe\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", unsigned(size));
            if (httpd_resp_send_chunk(req, header, n) != ESP_OK ||
                httpd_resp_send_chunk(req, reinterpret_cast<const char *>(http_image), size) != ESP_OK)
                return ESP_FAIL;
        }
        if (esp_timer_get_time() - last_sent > 5000000) return ESP_FAIL;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
esp_err_t CameraService::retryHandler(httpd_req_t *req) {
    // 失败后的任务已释放DMA、图像内存并让C8休眠；可以修好接线后重试，无须重新烧录。
    esp_err_t error = start();
    if (error != ESP_OK) httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_sendstr(req, esp_err_to_name(error));
}
esp_err_t CameraService::registersHandler(httpd_req_t *req) {
    int values[sizeof(color_regs)/sizeof(color_regs[0])];
    portENTER_CRITICAL(&stats_lock);
    std::memcpy(values,color_reg_values,sizeof(values));
    portEXIT_CRITICAL(&stats_lock);
    char json[1024]; size_t used=0;
    json[used++]='{';
    for(unsigned i=0;i<sizeof(color_regs)/sizeof(color_regs[0]);++i)
        used+=std::snprintf(json+used,sizeof(json)-used,"%s\"%04x\":%d",i?",":"",color_regs[i],values[i]);
    json[used++]='}';
    httpd_resp_set_type(req,"application/json");
    return httpd_resp_send(req,json,used);
}
esp_err_t CameraService::colorHandler(httpd_req_t *req) {
    char query[32], value[8], *end = nullptr;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "mode", value, sizeof(value)) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "mode required");
    long mode = std::strtol(value, &end, 10);
    if (end == value || *end || mode < 0 || mode > 8)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "mode must be 0..8");
    requested_color.store(mode);
    return httpd_resp_sendstr(req, "queued");
}
esp_err_t CameraService::focusHandler(httpd_req_t *req) {
    focus_requested.store(true);
    return httpd_resp_sendstr(req, "queued");
}
esp_err_t CameraService::profileHandler(httpd_req_t *req) {
    char query[32], value[8], *end = nullptr;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "id", value, sizeof(value)) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "profile id required");
    long profile = std::strtol(value, &end, 10);
    if (end == value || *end || profile < 0 || profile > 20)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "profile id must be 0..20");
    requested_profile.store(profile);
    return httpd_resp_sendstr(req, "queued");
}
esp_err_t CameraService::registerControlRoutes(httpd_handle_t server) {
    httpd_uri_t status{};
    status.uri = "/camera/status"; status.method = HTTP_GET; status.handler = statusHandler;
    esp_err_t error = httpd_register_uri_handler(server, &status);
    httpd_uri_t preview{};
    preview.uri = "/camera"; preview.method = HTTP_GET; preview.handler = previewHandler;
    if (error == ESP_OK) error = httpd_register_uri_handler(server, &preview);
    httpd_uri_t retry{};
    retry.uri = "/camera/retry"; retry.method = HTTP_POST; retry.handler = retryHandler;
    if (error == ESP_OK) error = httpd_register_uri_handler(server, &retry);
    httpd_uri_t profile{};
    profile.uri = "/camera/profile"; profile.method = HTTP_POST; profile.handler = profileHandler;
    if (error == ESP_OK) error = httpd_register_uri_handler(server, &profile);
    httpd_uri_t focus{};
    focus.uri = "/camera/focus"; focus.method = HTTP_POST; focus.handler = focusHandler;
    if (error == ESP_OK) error = httpd_register_uri_handler(server, &focus);
    httpd_uri_t color{};
    color.uri = "/camera/color"; color.method = HTTP_POST; color.handler = colorHandler;
    if (error == ESP_OK) error = httpd_register_uri_handler(server, &color);
    httpd_uri_t regs{};
    regs.uri = "/camera/registers"; regs.method = HTTP_GET; regs.handler = registersHandler;
    if (error == ESP_OK) error = httpd_register_uri_handler(server, &regs);
    return error;
}
