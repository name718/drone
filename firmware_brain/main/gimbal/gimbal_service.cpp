/**
 * @file gimbal_service.cpp
 * @brief 机器人二自由度头部云台业务服务实现
 */
#include "gimbal/gimbal_service.hpp"

#include <cmath>
#include "config/board_config.hpp"
#include "esp_log.h"

static const char *TAG = "云台服务";

GimbalService &GimbalService::getInstance() {
    static GimbalService instance;
    return instance;
}

GimbalService::GimbalService() = default;

esp_err_t GimbalService::init() {
    // 使用统一配置引脚初始化底层驱动
    return driver_.init(Config::Gimbal::PIN_SERVO_PAN, Config::Gimbal::PIN_SERVO_TILT);
}

void GimbalService::lookAt(float pan, float tilt) {
    // 打断当前可能正在播放的手势，切为用户指令目标
    active_gesture_ = GimbalGesture::NONE;
    target_pan_ = pan;
    target_tilt_ = tilt;
}

void GimbalService::nod() {
    active_gesture_ = GimbalGesture::NOD;
    gesture_step_ = 0;
    gesture_tick_count_ = 0;
    ESP_LOGI(TAG, "执行拟人动作: 点头");
}

void GimbalService::shake() {
    active_gesture_ = GimbalGesture::SHAKE;
    gesture_step_ = 0;
    gesture_tick_count_ = 0;
    ESP_LOGI(TAG, "执行拟人动作: 摇头");
}

void GimbalService::reset() {
    active_gesture_ = GimbalGesture::NONE;
    target_pan_ = Config::Gimbal::DEFAULT_PAN_ANGLE;
    target_tilt_ = Config::Gimbal::DEFAULT_TILT_ANGLE;
    ESP_LOGI(TAG, "云台回正居中 (Pan=%.1f°, Tilt=%.1f°)", target_pan_.load(), target_tilt_.load());
}

void GimbalService::updateGesture() {
    if (active_gesture_ == GimbalGesture::NONE) return;

    gesture_tick_count_++;

    if (active_gesture_ == GimbalGesture::NOD) {
        // 点头手势状态机 (每 10 帧 = 200ms 推进一个动作相位，围绕 Tilt=0° 开展点头波形)
        constexpr uint32_t STEP_TICKS = 10;
        if (gesture_tick_count_ >= STEP_TICKS) {
            gesture_tick_count_ = 0;
            gesture_step_++;

            switch (gesture_step_) {
                case 1:
                    target_tilt_ = 25.0f;  // 低头
                    break;
                case 2:
                    target_tilt_ = 5.0f;   // 抬起
                    break;
                case 3:
                    target_tilt_ = 18.0f;  // 再次微低头
                    break;
                case 4:
                    target_tilt_ = Config::Gimbal::DEFAULT_TILT_ANGLE;  // 回归基准仰角 0度
                    break;
                default:
                    active_gesture_ = GimbalGesture::NONE;  // 动作播放完成
                    break;
            }
        }
    } else if (active_gesture_ == GimbalGesture::SHAKE) {
        // 摇头手势状态机 (每 9 帧 = 180ms 推进一个动作相位，围绕 Pan=29° 对称左右摆动)
        constexpr uint32_t STEP_TICKS = 9;
        if (gesture_tick_count_ >= STEP_TICKS) {
            gesture_tick_count_ = 0;
            gesture_step_++;

            switch (gesture_step_) {
                case 1:
                    target_pan_ = 9.0f;   // 向左转动 (-20度)
                    break;
                case 2:
                    target_pan_ = 52.0f;  // 向右转动 (+23度)
                    break;
                case 3:
                    target_pan_ = 17.0f;  // 再次向左轻晃
                    break;
                case 4:
                    target_pan_ = Config::Gimbal::DEFAULT_PAN_ANGLE;   // 回归中央基准 29度
                    break;
                default:
                    active_gesture_ = GimbalGesture::NONE;  // 动作播放完成
                    break;
            }
        }
    }
}

void GimbalService::gimbalTask(void *param) {
    auto *self = static_cast<GimbalService *>(param);
    ESP_LOGI(TAG, "云台 50Hz 独立控制线程已启动并在 Core 1 运行");

    while (self->is_running_) {
        // 1. 推进手势动作时间轴
        self->updateGesture();

        // 2. 指数平滑加减速滤波器 (一阶低通滤波算法)
        // 滤波公式: current = current + (target - current) * alpha
        // 0.18f 的滤波因子兼顾响应灵敏度与机械阻尼柔顺度，消除 SG90 塑料齿轮的剧烈颤抖
        float t_pan = self->target_pan_.load();
        float c_pan = self->current_pan_.load();
        float diff_p = t_pan - c_pan;
        if (std::abs(diff_p) > 0.1f) {
            c_pan += diff_p * 0.18f;
            self->current_pan_.store(c_pan);
        } else {
            c_pan = t_pan;
            self->current_pan_.store(c_pan);
        }

        float t_tilt = self->target_tilt_.load();
        float c_tilt = self->current_tilt_.load();
        float diff_t = t_tilt - c_tilt;
        if (std::abs(diff_t) > 0.1f) {
            c_tilt += diff_t * 0.18f;
            self->current_tilt_.store(c_tilt);
        } else {
            c_tilt = t_tilt;
            self->current_tilt_.store(c_tilt);
        }

        // 3. 将滤波后的平滑角度输出给物理舵机
        self->driver_.setPanAngle(c_pan);
        self->driver_.setTiltAngle(c_tilt);

        // 4. 精确休眠 20ms (保持 50Hz 控制刷新率)
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    vTaskDelete(nullptr);
}

esp_err_t GimbalService::start() {
    if (is_running_) return ESP_OK;

    is_running_ = true;
    BaseType_t ret = xTaskCreatePinnedToCore(
        gimbalTask,
        Config::Tasks::GIMBAL_TASK_NAME,
        Config::Tasks::GIMBAL_STACK_SIZE,
        this,
        Config::Tasks::GIMBAL_PRIORITY,
        &task_handle_,
        Config::Tasks::GIMBAL_CORE_ID  // 绑定在核心 1
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建云台后台任务失败！");
        is_running_ = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "云台服务启动成功！");
    return ESP_OK;
}
