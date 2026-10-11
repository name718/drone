/**
 * @file interaction_service.cpp
 * @brief 声-画-机多模态拟人自主交互行为引擎实现文件
 */
#include "core/interaction_service.hpp"

#include <cmath>

#include "ai/ai_service.hpp"
#include "config/board_config.hpp"
#include "esp_log.h"
#include "esp_random.h"
#include "network/wifi_manager.hpp"

static const char *TAG = "交互引擎";

InteractionService &InteractionService::getInstance() {
    static InteractionService instance;
    return instance;
}

InteractionService::InteractionService() = default;

InteractionService::~InteractionService() {
    is_running_ = false;
}

esp_err_t InteractionService::init() {
    ESP_LOGI(TAG, "正在初始化拟人多模态交互引擎...");
    uint64_t now = esp_timer_get_time() / 1000ULL;
    last_activity_time_ms_ = now;
    last_sound_trigger_ms_ = now;
    next_micro_action_ms_ = now + 10000ULL;  // 10 秒后开启首次自主微动作
    is_sleeping_ = false;
    is_in_sound_reaction_ = false;

    ESP_LOGI(TAG, "拟人多模态交互引擎参数初始化完成！");
    return ESP_OK;
}

esp_err_t InteractionService::start() {
    if (is_running_) return ESP_OK;

    is_running_ = true;

    // 创建后台 10Hz 决策任务，绑定到 Core 0 (低优先级，平稳轮询)
    BaseType_t ret = xTaskCreatePinnedToCore(
        interactionTask,
        Config::Tasks::INTERACTION_TASK_NAME,
        Config::Tasks::INTERACTION_STACK_SIZE,
        this,
        Config::Tasks::INTERACTION_PRIORITY,
        &task_handle_,
        Config::Tasks::INTERACTION_CORE_ID  // 绑定 Core 0
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建交互引擎后台任务失败！");
        is_running_ = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "拟人多模态交互引擎已在 Core 0 启动运行！");
    return ESP_OK;
}

void InteractionService::setAutonomousMode(bool enable) {
    autonomous_mode_.store(enable);
    ESP_LOGI(TAG, "自主拟人生命感模式已切换为: %s", enable ? "【开启】" : "【关闭】");
    if (!enable) {
        // 关闭自主模式时，重置所有临时状态
        is_in_sound_reaction_ = false;
        is_sleeping_ = false;
    } else {
        notifyUserActivity();
    }
}

void InteractionService::notifyUserActivity() {
    last_activity_time_ms_ = esp_timer_get_time() / 1000ULL;
    if (is_sleeping_) {
        // 用户操作将机器人从打盹中瞬间唤醒
        is_sleeping_ = false;
        triggerBehavior(RobotBehavior::NORMAL);
        ESP_LOGI(TAG, "用户操作介入，机器人已从打盹中苏醒！");
    }
}

void InteractionService::triggerBehavior(RobotBehavior behavior) {
    auto &display = DisplayService::getInstance();
    auto &gimbal = GimbalService::getInstance();
    auto &audio = AudioService::getInstance();

    switch (behavior) {
        case RobotBehavior::NORMAL:
            display.setEmotion(EmotionState::NORMAL);
            gimbal.reset();
            break;

        case RobotBehavior::WAKE_UP:
            ESP_LOGI(TAG, "触发【声音唤醒】拟人反应！(睁大双眼 + 仰头注视倾听)");
            display.setEmotion(EmotionState::SURPRISED);
            // 头部微仰 16 度，展现被声音吸引的好奇注视姿态 (不鸣笛，避免杂音干扰录音麦克风)
            gimbal.lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE, 16.0f);
            break;

        case RobotBehavior::HAPPY:
            ESP_LOGI(TAG, "触发【开心欢喜】联动！(弯弯笑眼 + 活泼晃头 + 双音大调)");
            display.setEmotion(EmotionState::HAPPY);
            gimbal.shake();  // 欢快摆头
            // 演奏两段欢快大调音符 (G5 -> C6)
            audio.playTone(783.99f, 70);
            vTaskDelay(pdMS_TO_TICKS(80));
            audio.playTone(1046.50f, 130);
            break;

        case RobotBehavior::ALERT:
            ESP_LOGI(TAG, "触发【警觉避险】联动！(警惕眼神 + 紧急提示音 + 姿态居中)");
            display.setEmotion(EmotionState::NORMAL);
            gimbal.reset();
            audio.playAlert();
            break;

        case RobotBehavior::SLEEP:
            ESP_LOGI(TAG, "触发【困倦打盹】挂机！(眯眼打盹 + 低头放平)");
            display.setEmotion(EmotionState::SLEEPY);
            // 头部微垂至 0 度，安静休眠
            gimbal.lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE, 0.0f);
            break;

        case RobotBehavior::NOD:
            ESP_LOGI(TAG, "触发【点头认同】联动！");
            display.setEmotion(EmotionState::HAPPY);
            gimbal.nod();
            audio.playTone(880.0f, 70);
            break;

        case RobotBehavior::SHAKE:
            ESP_LOGI(TAG, "触发【摇头否定】联动！");
            display.setEmotion(EmotionState::NORMAL);
            gimbal.shake();
            audio.playTone(392.0f, 90);
            break;

        case RobotBehavior::LOVE:
            ESP_LOGI(TAG, "触发【爱心心动】联动！(跳动粉红爱心 + 娇羞偏头 + 甜美双音)");
            display.setEmotion(EmotionState::LOVE);
            gimbal.lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE - 6.0f, 10.0f);
            audio.playTone(659.25f, 80);
            vTaskDelay(pdMS_TO_TICKS(90));
            audio.playTone(783.99f, 120);
            break;

        case RobotBehavior::ANGRY:
            ESP_LOGI(TAG, "触发【生气戒备】联动！(红光斜眉怒眼 + 低视怒瞪 + 警示音)");
            display.setEmotion(EmotionState::ANGRY);
            gimbal.lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE, -5.0f);
            audio.playTone(220.0f, 70);
            vTaskDelay(pdMS_TO_TICKS(80));
            audio.playTone(185.0f, 90);
            break;

        case RobotBehavior::CONFUSED:
            ESP_LOGI(TAG, "触发【疑惑挑眉】联动！(不对称挑眉 + 歪头探脑 + 问号音)");
            display.setEmotion(EmotionState::CONFUSED);
            gimbal.lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE + 14.0f, 12.0f);
            audio.playTone(587.33f, 70);
            vTaskDelay(pdMS_TO_TICKS(80));
            audio.playTone(880.00f, 110);
            break;

        case RobotBehavior::DIZZY:
            ESP_LOGI(TAG, "触发【眩晕打转】联动！(旋转蚊香圈 + 晃头 + 摇摆音)");
            display.setEmotion(EmotionState::DIZZY);
            gimbal.lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE - 12.0f, 6.0f);
            audio.playTone(523.25f, 80);
            vTaskDelay(pdMS_TO_TICKS(80));
            audio.playTone(392.00f, 120);
            break;

        case RobotBehavior::DANCE:
            triggerDance();
            break;

        case RobotBehavior::CURIOUS:
            ESP_LOGI(TAG, "触发【好奇探头】联动！(不对称歪头 + 微微环视 + 灵动挑眉音)");
            display.setEmotion(EmotionState::CONFUSED);
            gimbal.lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE + 14.0f, 15.0f);
            audio.playTone(659.25f, 60);
            vTaskDelay(pdMS_TO_TICKS(70));
            audio.playTone(880.00f, 90);
            break;

        case RobotBehavior::SHY:
            ESP_LOGI(TAG, "触发【害羞娇憨】联动！(低头侧偏 + 爱心眼神 + 柔和双音)");
            display.setEmotion(EmotionState::LOVE);
            gimbal.lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE - 10.0f, -4.0f);
            audio.playTone(523.25f, 70);
            vTaskDelay(pdMS_TO_TICKS(80));
            audio.playTone(659.25f, 100);
            break;

        case RobotBehavior::PATROL:
            ESP_LOGI(TAG, "触发【警惕巡视】联动！(左右缓缓环视扫描)");
            xTaskCreatePinnedToCore(
                [](void *) {
                    auto &gimbal = GimbalService::getInstance();
                    auto &display = DisplayService::getInstance();
                    display.setEmotion(EmotionState::NORMAL);
                    gimbal.lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE - 22.0f, 5.0f);
                    vTaskDelay(pdMS_TO_TICKS(400));
                    gimbal.lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE + 22.0f, 5.0f);
                    vTaskDelay(pdMS_TO_TICKS(400));
                    gimbal.reset();
                    vTaskDelete(nullptr);
                }, "PatrolTask", 2048, nullptr, 3, nullptr, 0);
            break;

        case RobotBehavior::EXCITED:
            ESP_LOGI(TAG, "触发【兴奋雀跃】联动！(快速欢快点头 + 大调音阶)");
            display.setEmotion(EmotionState::HAPPY);
            gimbal.nod();
            audio.playTone(523.25f, 60);   // C5
            vTaskDelay(pdMS_TO_TICKS(65));
            audio.playTone(659.25f, 60);   // E5
            vTaskDelay(pdMS_TO_TICKS(65));
            audio.playTone(783.99f, 60);   // G5
            vTaskDelay(pdMS_TO_TICKS(65));
            audio.playTone(1046.50f, 120); // C6
            break;
    }
}

void InteractionService::triggerDance() {
    ESP_LOGI(TAG, "启动赛博跳舞特技大秀 (Music & Dance Routine)！");
    // 异步创建一次性特技表演任务，避免阻塞主业务线程
    xTaskCreatePinnedToCore(
        [](void *param) {
            auto *self = static_cast<InteractionService *>(param);
            auto &display = DisplayService::getInstance();
            auto &gimbal = GimbalService::getInstance();
            auto &audio = AudioService::getInstance();

            // 节拍 1: 准备起舞 - 疑惑探头
            display.setEmotion(EmotionState::CONFUSED);
            gimbal.lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE + 14.0f, 8.0f);
            audio.playTone(523.25f, 100);  // C5
            vTaskDelay(pdMS_TO_TICKS(130));

            // 节拍 2: 动感节奏展开 - 左右卡点律动
            display.setEmotion(EmotionState::HAPPY);
            gimbal.lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE + 18.0f, 16.0f);
            audio.playTone(659.25f, 100);  // E5
            vTaskDelay(pdMS_TO_TICKS(130));

            gimbal.lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE - 18.0f, 2.0f);
            audio.playTone(783.99f, 100);  // G5
            vTaskDelay(pdMS_TO_TICKS(130));

            // 节拍 3: 高潮电音段 - 爱心心动 + 快速点头
            display.setEmotion(EmotionState::LOVE);
            gimbal.nod();
            audio.playTone(1046.50f, 140);  // C6
            vTaskDelay(pdMS_TO_TICKS(160));
            audio.playTone(880.00f, 100);   // A5
            vTaskDelay(pdMS_TO_TICKS(130));

            // 节拍 4: 旋转蚊香圈眩晕摆头
            display.setEmotion(EmotionState::DIZZY);
            gimbal.shake();
            audio.playTone(659.25f, 90);
            vTaskDelay(pdMS_TO_TICKS(110));
            audio.playTone(523.25f, 180);
            vTaskDelay(pdMS_TO_TICKS(220));

            // 收尾优雅回正，刷新活跃时间戳
            display.setEmotion(EmotionState::NORMAL);
            gimbal.reset();
            self->notifyUserActivity();

            vTaskDelete(nullptr);
        },
        "DanceRoutine",
        4096,
        this,
        4,
        nullptr,
        0
    );
}

void InteractionService::handleHapticTap() {
    ESP_LOGI(TAG, "处理体感【敲击桌面/拍头】事件！");
    notifyUserActivity();

    auto &display = DisplayService::getInstance();
    auto &gimbal = GimbalService::getInstance();
    auto &audio = AudioService::getInstance();

    if (is_sleeping_) {
        // 从熟睡中被敲醒：震惊睁眼 + 抬头看天
        is_sleeping_ = false;
        triggerBehavior(RobotBehavior::WAKE_UP);
    } else {
        // 常态下被敲击：惊吓/好奇挑眉 + 向上微微仰望 + 灵动双音
        display.setEmotion(EmotionState::SURPRISED);
        gimbal.lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE, 12.0f);
        audio.playTone(880.00f, 60);
        vTaskDelay(pdMS_TO_TICKS(70));
        audio.playTone(1174.66f, 100);
    }
}

void InteractionService::handleHapticPickup(bool is_picked_up) {
    auto &display = DisplayService::getInstance();
    auto &gimbal = GimbalService::getInstance();
    auto &audio = AudioService::getInstance();

    notifyUserActivity();

    if (is_picked_up) {
        ESP_LOGI(TAG, "处理体感【悬空抱起/失重】事件 -> 触发悬空挣扎与求救");
        is_sleeping_ = false;
        display.setEmotion(EmotionState::DIZZY);
        // 低头俯瞰地面，四顾慌张
        gimbal.lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE, -12.0f);
        audio.playTone(493.88f, 70);
        vTaskDelay(pdMS_TO_TICKS(80));
        audio.playTone(369.99f, 100);
    } else {
        ESP_LOGI(TAG, "处理体感【平稳放回桌面】事件 -> 舒缓获救与开心常态");
        triggerBehavior(RobotBehavior::HAPPY);
        audio.playTone(523.25f, 70);
        vTaskDelay(pdMS_TO_TICKS(80));
        audio.playTone(659.25f, 110);
    }
}

void InteractionService::handleHapticPush() {
    ESP_LOGI(TAG, "处理体感【被动推车滑行】事件！");
    notifyUserActivity();

    auto &display = DisplayService::getInstance();
    auto &audio = AudioService::getInstance();

    display.setEmotion(EmotionState::HAPPY);
    audio.playTone(783.99f, 60);
}

void InteractionService::handleHapticFall() {
    ESP_LOGW(TAG, "处理体感【摔倒倾覆】事件 -> 触发呼救警报");
    auto &display = DisplayService::getInstance();
    auto &audio = AudioService::getInstance();

    display.setEmotion(EmotionState::DIZZY);
    audio.playTone(329.63f, 120);
    vTaskDelay(pdMS_TO_TICKS(130));
    audio.playTone(261.63f, 160);
}

void InteractionService::interactionTask(void *param) {
    auto *self = static_cast<InteractionService *>(param);
    ESP_LOGI(TAG, "拟人交互后台行为决策任务已在 Core 0 启动 (10Hz)");

    constexpr float SOUND_WAKEUP_THRESHOLD = 0.065f; // 声音唤醒阈值
    constexpr uint64_t SOUND_COOLDOWN_MS = 1500;     // 唤醒保护冷却 (1.5 秒)

    // 桌宠自主生命周期阶段定义
    constexpr uint64_t PET_BORED_MS   = 30000;       // 30 秒无操作: 进入无聊探望阶段
    constexpr uint64_t PET_DROWSY_MS  = 90000;       // 90 秒无操作: 进入困倦打瞌睡阶段
    constexpr uint64_t PET_SLEEP_MS   = 180000;      // 180 秒无操作: 进入深睡打鼾休眠

    while (self->is_running_) {
        uint64_t now = esp_timer_get_time() / 1000ULL;  // 当前系统开机毫秒数

        // 仅在自主拟人模式开启时，由大脑自主触发行为联动
        if (self->autonomous_mode_.load()) {
            float energy = AudioService::getInstance().getMicEnergy();

            // 1. 声音感知与唤醒检测
            if (energy >= SOUND_WAKEUP_THRESHOLD && (now - self->last_sound_trigger_ms_) > SOUND_COOLDOWN_MS) {
                // 如果 AI 当前未在思考、播报或聆听中，且扬声器未在发声，则响应声音唤醒
                if (!AiService::getInstance().isBusy() && !AudioService::getInstance().isPlaying()) {
                    self->last_sound_trigger_ms_ = now;
                    self->last_activity_time_ms_ = now;
                    self->is_in_sound_reaction_ = true;
                    self->is_sleeping_ = false;
                    self->wakeup_return_normal_ms_ = now + 4000;

                    if (WifiManager::getInstance().isConnected()) {
                        ESP_LOGI(TAG, "检测到有效声音刺激 (能量: %.2f)，触发 AI 语音倾听与大模型交互", energy);
                        AiService::getInstance().triggerVoiceListen();
                    } else {
                        ESP_LOGI(TAG, "检测到声音刺激 (能量: %.2f)，未联网，仅执行基础拟人动作唤醒", energy);
                        self->triggerBehavior(RobotBehavior::WAKE_UP);
                    }
                }
            }

            // 2. 声音唤醒后恢复常态检测
            if (self->is_in_sound_reaction_ && now >= self->wakeup_return_normal_ms_) {
                if (!AiService::getInstance().isBusy()) {
                    self->is_in_sound_reaction_ = false;
                    self->triggerBehavior(RobotBehavior::NORMAL);
                    ESP_LOGI(TAG, "声音刺激结束，恢复平静常态。");
                } else {
                    // AI 服务仍在流式对话或朗读中，顺延复位时间
                    self->wakeup_return_normal_ms_ = now + 1000;
                }
            }

            // 3. 桌宠四阶段生命周期状态机 (探索 -> 无聊巡视 -> 困倦点头 -> 熟睡打鼾)
            if (!self->is_in_sound_reaction_) {
                uint64_t idle_time = now - self->last_activity_time_ms_;

                if (idle_time >= PET_SLEEP_MS) {
                    // 【阶段 4: 深睡打鼾休眠】
                    if (!self->is_sleeping_) {
                        self->is_sleeping_ = true;
                        self->triggerBehavior(RobotBehavior::SLEEP);
                    }
                } else if (idle_time >= PET_DROWSY_MS) {
                    // 【阶段 3: 困倦打瞌睡】半闭大眼 + 垂头轻微打盹
                    if (!self->is_sleeping_ && now >= self->next_micro_action_ms_) {
                        DisplayService::getInstance().setEmotion(EmotionState::SLEEPY);
                        GimbalService::getInstance().lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE, -4.0f);
                        self->next_micro_action_ms_ = now + 12000ULL + (esp_random() % 6000);
                    }
                } else if (idle_time >= PET_BORED_MS) {
                    // 【阶段 2: 无聊巡视】左右缓缓四顾
                    if (now >= self->next_micro_action_ms_) {
                        self->triggerBehavior(RobotBehavior::PATROL);
                        self->next_micro_action_ms_ = now + 15000ULL + (esp_random() % 8000);
                    }
                } else {
                    // 【阶段 1: 活跃探索】每隔 7~12 秒做自发生命微动作
                    if (now >= self->next_micro_action_ms_) {
                        uint32_t act_type = esp_random() % 10;
                        if (act_type < 6) {
                            // 随机微调水平角度 (-6° ~ +6°) 与俯仰角度 (0° ~ 8°)
                            int pan_offset = static_cast<int>(esp_random() % 13) - 6;
                            float micro_pan = Config::Gimbal::DEFAULT_PAN_ANGLE + static_cast<float>(pan_offset);
                            float micro_tilt = static_cast<float>(esp_random() % 9);
                            GimbalService::getInstance().lookAt(micro_pan, micro_tilt);
                        } else if (act_type < 8) {
                            // 好奇微探头
                            GimbalService::getInstance().lookAt(Config::Gimbal::DEFAULT_PAN_ANGLE + 10.0f, 10.0f);
                            DisplayService::getInstance().setEmotion(EmotionState::CONFUSED);
                        } else {
                            // 柔和回正与大眼常态
                            GimbalService::getInstance().reset();
                            DisplayService::getInstance().setEmotion(EmotionState::NORMAL);
                        }

                        self->next_micro_action_ms_ = now + 7000ULL + (esp_random() % 5000);
                    }
                }
            }
        }

        // 100ms 决策拍频 (10Hz)
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    vTaskDelete(nullptr);
}
