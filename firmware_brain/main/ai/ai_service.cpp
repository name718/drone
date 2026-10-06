/**
 * @file ai_service.cpp
 * @brief 机器人云端大模型 (LLM) 与语音合成 (CosyVoice TTS) 核心服务实现文件
 */
#include "ai/ai_service.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <regex>
#include <map>
#include <vector>

#include "audio/audio_service.hpp"
#include "cJSON.h"
#include "comm/chassis_service.hpp"
#include "config/board_config.hpp"
#include "core/interaction_service.hpp"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "display/display_service.hpp"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "gimbal/gimbal_service.hpp"
#include "network/web_server.hpp"
#include "network/wifi_manager.hpp"

static const char *TAG = "AI服务";

// 移除文本中所有的 UTF-8 emoji 与装饰图标字符
static std::string stripEmojisAndIcons(const std::string &str) {
    std::string clean;
    clean.reserve(str.size());
    size_t i = 0;
    while (i < str.size()) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if (c < 0x80) {
            clean += str[i++];
        } else if ((c & 0xE0) == 0xC0) {
            // 2 字节 UTF-8
            if (i + 1 < str.size()) {
                clean += str[i];
                clean += str[i + 1];
                i += 2;
            } else {
                break;
            }
        } else if ((c & 0xF0) == 0xE0) {
            // 3 字节 UTF-8: 过滤特殊杂项符号与变体选择符
            if (i + 2 < str.size()) {
                unsigned char c2 = static_cast<unsigned char>(str[i + 1]);
                if (c == 0xE2 && (c2 >= 0x90 && c2 <= 0xBF)) {
                    i += 3;
                } else if (c == 0xEF && c2 == 0xB8) {
                    i += 3;
                } else {
                    clean += str[i];
                    clean += str[i + 1];
                    clean += str[i + 2];
                    i += 3;
                }
            } else {
                break;
            }
        } else if ((c & 0xF8) == 0xF0) {
            // 4 字节 UTF-8: 标准 Emoji
            i += std::min<size_t>(4, str.size() - i);
        } else {
            i++;
        }
    }
    return clean;
}

// LLM HTTP 响应分片接收事件回调
static esp_err_t llmHttpEventHandler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA: {
            auto *response_str = static_cast<std::string *>(evt->user_data);
            if (response_str && evt->data && evt->data_len > 0) {
                response_str->append(static_cast<const char *>(evt->data), evt->data_len);
            }
            break;
        }
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "LLM HTTP 传输层发生异常");
            break;
        default:
            break;
    }
    return ESP_OK;
}

enum class AiCommandType {
    TEXT_PROMPT,
    VOICE_LISTEN
};

struct AiCommand {
    AiCommandType type;
    std::string text;
};

// TTS 会话状态上下文
struct TtsSessionContext {
    std::atomic<bool> is_connected{false};
    std::atomic<bool> is_finished{false};
    std::atomic<bool> has_error{false};
    size_t total_bytes_received{0};
};

// ASR 会话状态上下文
struct AsrSessionContext {
    std::atomic<bool> is_connected{false};
    std::atomic<bool> is_started{false};
    std::atomic<bool> is_finished{false};
    std::atomic<bool> has_error{false};
    std::atomic<bool> sentence_end_detected{false};
    std::map<int, std::string> sentence_map;
    std::string recognized_text;
};

static void asrWebsocketEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    auto *ctx = static_cast<AsrSessionContext *>(handler_args);
    auto *data = static_cast<esp_websocket_event_data_t *>(event_data);

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "DashScope ASR WebSocket 连接握手成功！");
            ctx->is_connected.store(true);
            break;

        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == 0x01 && data->data_ptr && data->data_len > 0) {
                cJSON *root = cJSON_ParseWithLength(data->data_ptr, data->data_len);
                if (root) {
                    cJSON *header = cJSON_GetObjectItem(root, "header");
                    if (header) {
                        cJSON *event = cJSON_GetObjectItem(header, "event");
                        if (event && cJSON_IsString(event) && event->valuestring) {
                            if (strcmp(event->valuestring, "task-started") == 0) {
                                ESP_LOGI(TAG, "ASR task-started 任务启动成功");
                                ctx->is_started.store(true);
                            } else if (strcmp(event->valuestring, "result-generated") == 0) {
                                cJSON *payload = cJSON_GetObjectItem(root, "payload");
                                if (payload) {
                                    cJSON *output = cJSON_GetObjectItem(payload, "output");
                                    if (output) {
                                        auto update_single_sentence = [&](cJSON *item) {
                                            if (!item) return;
                                            int sentence_id = 0;
                                            cJSON *id_obj = cJSON_GetObjectItem(item, "sentence_id");
                                            if (id_obj && cJSON_IsNumber(id_obj)) {
                                                sentence_id = id_obj->valueint;
                                            }
                                            cJSON *text_obj = cJSON_GetObjectItem(item, "text");
                                            if (text_obj && cJSON_IsString(text_obj) && text_obj->valuestring) {
                                                ctx->sentence_map[sentence_id] = text_obj->valuestring;
                                            }
                                            cJSON *end_obj = cJSON_GetObjectItem(item, "sentence_end");
                                            if (end_obj && cJSON_IsBool(end_obj) && cJSON_IsTrue(end_obj)) {
                                                ctx->sentence_end_detected.store(true);
                                            }
                                        };

                                        cJSON *sentence = cJSON_GetObjectItem(output, "sentence");
                                        if (sentence) {
                                            if (cJSON_IsArray(sentence)) {
                                                int arr_size = cJSON_GetArraySize(sentence);
                                                for (int i = 0; i < arr_size; i++) {
                                                    update_single_sentence(cJSON_GetArrayItem(sentence, i));
                                                }
                                            } else if (cJSON_IsObject(sentence)) {
                                                update_single_sentence(sentence);
                                            }
                                        } else {
                                            cJSON *text = cJSON_GetObjectItem(output, "text");
                                            if (text && cJSON_IsString(text) && text->valuestring) {
                                                ctx->sentence_map[0] = text->valuestring;
                                            }
                                        }

                                        // 顺序拼接所有句段，彻底防止分句被冲掉或丢前句
                                        std::string combined;
                                        for (const auto &pair : ctx->sentence_map) {
                                            combined += pair.second;
                                        }
                                        if (!combined.empty()) {
                                            ctx->recognized_text = combined;
                                        }
                                        ESP_LOGI(TAG, "ASR 语音流累积识别: \"%s\"", ctx->recognized_text.c_str());
                                    }
                                }
                            } else if (strcmp(event->valuestring, "task-finished") == 0) {
                                ESP_LOGI(TAG, "ASR task-finished 任务顺利结束，最终识别: \"%s\"", ctx->recognized_text.c_str());
                                ctx->is_finished.store(true);
                            } else if (strcmp(event->valuestring, "task-failed") == 0) {
                                ESP_LOGE(TAG, "ASR 任务报告失败: %.*s", data->data_len, data->data_ptr);
                                ctx->has_error.store(true);
                                ctx->is_finished.store(true);
                            }
                        }
                    }
                    cJSON_Delete(root);
                }
            }
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_ERROR:
            ctx->is_finished.store(true);
            break;

        default:
            break;
    }
}

AiService &AiService::getInstance() {
    static AiService instance;
    return instance;
}

AiService::AiService() = default;

AiService::~AiService() {
    if (task_handle_) {
        vTaskDelete(task_handle_);
    }
    if (command_queue_) {
        AiCommand *cmd = nullptr;
        while (xQueueReceive(command_queue_, &cmd, 0) == pdTRUE) {
            delete cmd;
        }
        vQueueDelete(command_queue_);
    }
}

esp_err_t AiService::init() {
    ESP_LOGI(TAG, "正在初始化云端 AI 调度服务...");

    command_queue_ = xQueueCreate(4, sizeof(AiCommand *));
    if (!command_queue_) {
        ESP_LOGE(TAG, "无法创建 AI 任务队列！");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t AiService::start() {
    BaseType_t ret = xTaskCreatePinnedToCore(
        aiTaskWorker,
        Config::Tasks::AI_TASK_NAME,
        Config::Tasks::AI_STACK_SIZE,
        this,
        Config::Tasks::AI_PRIORITY,
        &task_handle_,
        Config::Tasks::AI_CORE_ID);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "启动 AI 后台处理任务失败！");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "云端 AI 服务工作任务已成功启动并在 Core %d 运行", Config::Tasks::AI_CORE_ID);
    return ESP_OK;
}

void AiService::ask(const std::string &prompt) {
    if (prompt.empty() || !command_queue_) return;

    auto *cmd = new AiCommand{AiCommandType::TEXT_PROMPT, prompt};
    if (xQueueSend(command_queue_, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "AI 任务队列已满，丢弃最新文本提问");
        delete cmd;
    }
}

void AiService::triggerVoiceListen() {
    if (is_busy_.load() || !command_queue_) {
        return; // 正在发声、思考或已在倾听中，不重复触发
    }

    auto *cmd = new AiCommand{AiCommandType::VOICE_LISTEN, ""};
    if (xQueueSend(command_queue_, &cmd, 0) != pdTRUE) {
        delete cmd;
    }
}

void AiService::aiTaskWorker(void *param) {
    auto *self = static_cast<AiService *>(param);
    AiCommand *cmd = nullptr;

    while (true) {
        if (xQueueReceive(self->command_queue_, &cmd, portMAX_DELAY) == pdTRUE) {
            if (!cmd) continue;

            self->is_busy_.store(true);

            // 检查网络连接状态
            if (!WifiManager::getInstance().isConnected()) {
                ESP_LOGW(TAG, "Wi-Fi 尚未连接，无法请求云端大模型");
                WebServer::getInstance().broadcastAiReply("机器人尚未连接网络，请先配置 Wi-Fi。");
                WebServer::getInstance().broadcastAiStatus("idle");
                delete cmd;
                self->is_busy_.store(false);
                continue;
            }

            if (cmd->type == AiCommandType::VOICE_LISTEN) {
                delete cmd;
                self->processVoiceInteraction();
            } else if (cmd->type == AiCommandType::TEXT_PROMPT) {
                std::string prompt = cmd->text;
                delete cmd;
                self->processTextInteraction(prompt);
            }

            WebServer::getInstance().broadcastAiStatus("idle");
            self->is_busy_.store(false);
        }
    }
}

void AiService::processVoiceInteraction() {
    ESP_LOGI(TAG, "启动语音声控人机交互流程: 倾听用户说话...");
    WebServer::getInstance().broadcastAiStatus("listening");
    InteractionService::getInstance().triggerBehavior(RobotBehavior::WAKE_UP);

    // 录音并上传云端 ASR 识别
    std::string recognized = recordAndTranscribe();
    if (recognized.empty()) {
        ESP_LOGI(TAG, "未识别到有效语音文本，返回平静常态。");
        InteractionService::getInstance().triggerBehavior(RobotBehavior::NORMAL);
        return;
    }

    ESP_LOGI(TAG, "硬件麦克风语音识别原始文本: \"%s\"", recognized.c_str());

    uint64_t now_ms = esp_timer_get_time() / 1000ULL;
    // 15 秒多轮对话免唤醒会话窗口
    constexpr uint64_t ACTIVE_SESSION_TIMEOUT_MS = 15000;
    bool is_in_active_session = (last_interaction_time_ms_ > 0) &&
                                ((now_ms - static_cast<uint64_t>(last_interaction_time_ms_)) < ACTIVE_SESSION_TIMEOUT_MS);

    // 检查是否包含支持的前缀唤醒词 (小智同学, 小智, 小车小车, 小爱同学, 你好小智等)
    bool has_wake_word = false;
    std::string matched_wake;
    size_t wake_pos = std::string::npos;

    for (size_t i = 0; i < Config::CloudAI::WAKE_WORDS_COUNT; i++) {
        const char *w = Config::CloudAI::WAKE_WORDS[i];
        size_t pos = recognized.find(w);
        if (pos != std::string::npos) {
            has_wake_word = true;
            matched_wake = w;
            wake_pos = pos;
            break;
        }
    }

    // 过滤纯单字语气杂音与口癖
    auto is_noise_or_filler = [](const std::string &s) {
        if (s.empty()) return true;
        if (s == "嗯" || s == "啊" || s == "哦" || s == "呃" || s == "哼" || s == "呀" || s == "吧" || s == "呢") return true;
        if (s == "嗯。" || s == "啊。" || s == "哦。" || s == "呃。" || s == "？" || s == "。" || s == "，") return true;
        return false;
    };

    if (is_noise_or_filler(recognized)) {
        ESP_LOGI(TAG, "识别为单字语气杂音，忽略: \"%s\"", recognized.c_str());
        InteractionService::getInstance().triggerBehavior(RobotBehavior::NORMAL);
        return;
    }

    // 意图分析：若包含明确问答意图或常见口语指令，则判定为有效人机交互
    bool has_clear_intent = (recognized.find("吗") != std::string::npos ||
                             recognized.find("？") != std::string::npos ||
                             recognized.find("?") != std::string::npos ||
                             recognized.find("什么") != std::string::npos ||
                             recognized.find("谁") != std::string::npos ||
                             recognized.find("几") != std::string::npos ||
                             recognized.find("天") != std::string::npos ||
                             recognized.find("跳") != std::string::npos ||
                             recognized.find("唱") != std::string::npos ||
                             recognized.find("走") != std::string::npos ||
                             recognized.find("转") != std::string::npos ||
                             recognized.find("前进") != std::string::npos ||
                             recognized.find("后退") != std::string::npos ||
                             recognized.find("怎样") != std::string::npos ||
                             recognized.find("怎么") != std::string::npos ||
                             recognized.find("讲") != std::string::npos ||
                             recognized.find("说") != std::string::npos ||
                             recognized.find("你好") != std::string::npos ||
                             recognized.find("在吗") != std::string::npos ||
                             recognized.find("嗨") != std::string::npos);

    // 判定条件：匹配唤醒词 OR 多轮会话窗口期 OR 明确意图 OR 有效语句长度 >= 2个汉字 (>= 6 字节)
    bool is_valid_speech = has_wake_word || is_in_active_session || has_clear_intent || (recognized.length() >= 6);

    if (!is_valid_speech) {
        ESP_LOGI(TAG, "未检测到有效交互内容，忽略杂音: \"%s\"", recognized.c_str());
        InteractionService::getInstance().triggerBehavior(RobotBehavior::NORMAL);
        return;
    }

    // 提取纯有效提示词 (若携带唤醒词，则裁剪掉唤醒词前缀)
    std::string effective_prompt = recognized;
    if (has_wake_word) {
        effective_prompt.erase(wake_pos, matched_wake.length());
        // 清理裁剪后可能遗留的前置标点或空格
        while (!effective_prompt.empty()) {
            unsigned char c = static_cast<unsigned char>(effective_prompt.front());
            if (c == ' ' || c == '\t' || c == ',' || c == '?' || c == '!') {
                effective_prompt.erase(0, 1);
            } else if (effective_prompt.rfind("，", 0) == 0 ||
                       effective_prompt.rfind("。", 0) == 0 ||
                       effective_prompt.rfind("！", 0) == 0 ||
                       effective_prompt.rfind("？", 0) == 0 ||
                       effective_prompt.rfind("、", 0) == 0) {
                effective_prompt.erase(0, 3);
            } else {
                break;
            }
        }
    }

    // 刷新活跃交互时间戳，激活 15 秒多轮对话窗口
    last_interaction_time_ms_ = now_ms;

    // 将用户说的话以用户身份同步到 Web 控制台聊天区
    WebServer::getInstance().broadcastChatMessage(recognized, "user");

    // 若用户仅呼唤了名字 (如只说了 "小智同学" 或 "小爱同学")，直接秒回就绪问候，无需请求大模型
    if (effective_prompt.empty()) {
        ESP_LOGI(TAG, "用户仅唤醒呼叫，极速问候秒回...");
        WebServer::getInstance().broadcastAiStatus("speaking");
        std::string reply = "在呢！请问有什么吩咐？[EMOTION:HAPPY][ACTION:NOD]";
        std::string clean = dispatchActions(reply);
        WebServer::getInstance().broadcastAiReply(clean);
        streamTts(clean);
        return;
    }

    // 转入大模型思考与发声朗读流程
    processTextInteraction(effective_prompt);
}

std::string AiService::recordAndTranscribe() {
    ESP_LOGI(TAG, "开始建立 ASR WebSocket 会话进行麦克风拾音识别...");
    AudioService::getInstance().startAudioStream(12800); // 预提取 800ms 录音保全首字和唤醒词

    AsrSessionContext ctx;
    std::string auth_header = std::string("Authorization: Bearer ") + Config::CloudAI::API_KEY + "\r\n";

    esp_websocket_client_config_t ws_cfg = {};
    ws_cfg.uri = Config::CloudAI::ASR_WS_ENDPOINT;
    ws_cfg.headers = auth_header.c_str();
    ws_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    ws_cfg.buffer_size = 16384;
    ws_cfg.network_timeout_ms = 8000;

    esp_websocket_client_handle_t ws_client = esp_websocket_client_init(&ws_cfg);
    if (!ws_client) {
        ESP_LOGE(TAG, "创建 ASR WebSocket 客户端实例失败！");
        AudioService::getInstance().stopAudioStream();
        return "";
    }

    esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, asrWebsocketEventHandler, &ctx);

    esp_err_t ret = esp_websocket_client_start(ws_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "无法启动 ASR WebSocket 客户端: %s", esp_err_to_name(ret));
        esp_websocket_client_destroy(ws_client);
        AudioService::getInstance().stopAudioStream();
        return "";
    }

    // 1. 等待 WebSocket 连接握手 (超时 4 秒)
    int wait_conn_ms = 0;
    while (!ctx.is_connected.load() && !ctx.has_error.load() && wait_conn_ms < 4000) {
        vTaskDelay(pdMS_TO_TICKS(50));
        wait_conn_ms += 50;
    }

    if (!ctx.is_connected.load()) {
        ESP_LOGE(TAG, "连接 ASR WebSocket 超时！");
        esp_websocket_client_stop(ws_client);
        esp_websocket_client_destroy(ws_client);
        AudioService::getInstance().stopAudioStream();
        return "";
    }

    // 2. 发送 run-task 任务开启指令
    char task_id[32];
    snprintf(task_id, sizeof(task_id), "asr_%llu", static_cast<unsigned long long>(esp_timer_get_time()));

    cJSON *run_root = cJSON_CreateObject();
    cJSON *header = cJSON_CreateObject();
    cJSON_AddStringToObject(header, "action", "run-task");
    cJSON_AddStringToObject(header, "task_id", task_id);
    cJSON_AddStringToObject(header, "streaming", "duplex");
    cJSON_AddItemToObject(run_root, "header", header);

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "task_group", "audio");
    cJSON_AddStringToObject(payload, "task", "asr");
    cJSON_AddStringToObject(payload, "function", "recognition");
    cJSON_AddStringToObject(payload, "model", Config::CloudAI::ASR_MODEL);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "format", "pcm");
    cJSON_AddNumberToObject(params, "sample_rate", 16000);
    cJSON_AddNumberToObject(params, "max_sentence_silence", 1400);
    cJSON_AddBoolToObject(params, "disfluency_removal_enabled", false);

    cJSON *hints = cJSON_CreateArray();
    cJSON_AddItemToArray(hints, cJSON_CreateString("zh"));
    cJSON_AddItemToObject(params, "language_hints", hints);

    cJSON_AddItemToObject(payload, "parameters", params);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddItemToObject(payload, "input", input);

    cJSON_AddItemToObject(run_root, "payload", payload);

    char *run_json = cJSON_PrintUnformatted(run_root);
    cJSON_Delete(run_root);

    if (run_json) {
        esp_websocket_client_send_text(ws_client, run_json, strlen(run_json), portMAX_DELAY);
        cJSON_free(run_json);
    }

    // 3. 等待 task-started 响应 (超时 3 秒)
    int wait_started_ms = 0;
    while (!ctx.is_started.load() && !ctx.has_error.load() && wait_started_ms < 3000) {
        vTaskDelay(pdMS_TO_TICKS(50));
        wait_started_ms += 50;
    }

    if (!ctx.is_started.load()) {
        ESP_LOGE(TAG, "等待 ASR task-started 响应超时！");
        esp_websocket_client_stop(ws_client);
        esp_websocket_client_destroy(ws_client);
        AudioService::getInstance().stopAudioStream();
        return "";
    }

    ESP_LOGI(TAG, "ASR 会话就绪，进入实时连续流式音频推流...");

    // 4. 流式录音与静音 VAD 检测循环 (每次读取 1600 个采样点 = 100ms)
    constexpr size_t LIVE_CHUNK = 1600;
    std::vector<int16_t> live_buf(LIVE_CHUNK);

    int64_t start_time = esp_timer_get_time() / 1000;
    int64_t last_speech_time = start_time;
    bool speech_detected = false;

    // 语音能量检测门限 (去直流后静音能量约 0.01~0.03，设为 0.05f 捕捉清晰发音)
    constexpr float SPEECH_THRESHOLD = 0.05f;
    // 单次最长录音时长限制 (毫秒)
    constexpr int64_t MAX_RECORD_TIME_MS = 8000; // 8秒最大限制

    while (!ctx.has_error.load() && !ctx.is_finished.load()) {
        int64_t now = esp_timer_get_time() / 1000;
        int64_t elapsed = now - start_time;

        if (elapsed > MAX_RECORD_TIME_MS) {
            ESP_LOGI(TAG, "达到单次最大录音时限 (8s)，结束拾音");
            break;
        }

        size_t samples_read = 0;
        esp_err_t err = AudioService::getInstance().readAudioStream(live_buf.data(), LIVE_CHUNK, &samples_read, 120);
        if (err != ESP_OK || samples_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(15));
            continue;
        }

        // 计算 RMS 能量分贝
        double sum_squares = 0.0;
        for (size_t i = 0; i < samples_read; i++) {
            sum_squares += static_cast<double>(live_buf[i]) * static_cast<double>(live_buf[i]);
        }
        double rms = std::sqrt(sum_squares / static_cast<double>(samples_read));
        float energy = std::clamp(static_cast<float>(rms / 6000.0), 0.0f, 1.0f);
        AudioService::getInstance().setCurrentEnergy(energy);

        // 推送二进制 PCM 帧到 DashScope ASR
        int sent = esp_websocket_client_send_bin(ws_client,
                                                 reinterpret_cast<const char *>(live_buf.data()),
                                                 samples_read * sizeof(int16_t),
                                                 pdMS_TO_TICKS(200));
        if (sent < 0) {
            ESP_LOGE(TAG, "发送 ASR 音频流失败！");
            break;
        }

        // 语音活动检测 (VAD) 逻辑
        if (energy >= SPEECH_THRESHOLD) {
            if (!speech_detected) {
                ESP_LOGI(TAG, "检测到有效说话声音 (能量: %.2f)", energy);
            }
            speech_detected = true;
            last_speech_time = now;
        }

        if (speech_detected) {
            int64_t silence_duration = now - last_speech_time;
            bool has_recognized = !ctx.recognized_text.empty();

            // 超低延迟智能断句判定：
            // 1. 云端模型明确返回 sentence_end 分句完毕标志，且用户已停顿超过 400ms
            if (ctx.sentence_end_detected.load() && silence_duration >= 400 && elapsed >= 1200) {
                ESP_LOGI(TAG, "云端完成断句检测 (sentence_end=true)，提前极速结束拾音");
                break;
            }
            // 2. 已识别出文本且自然停顿超过 700ms (大幅缩短原本 1.4s 等待延迟)
            if (has_recognized && silence_duration >= 700 && elapsed >= 1200) {
                ESP_LOGI(TAG, "识别到完整语句且自然停顿超过 700ms，极速推入大模型理解");
                break;
            }
            // 3. 尚未拿到文字但说话后静音超过 1200ms
            if (silence_duration >= 1200 && elapsed >= 1500) {
                ESP_LOGI(TAG, "检测到说话停顿超过 1200ms，结束拾音");
                break;
            }
        } else {
            // 开始倾听后连续 3.5 秒仍无有效说话声音，超时退出
            if (elapsed > 3500) {
                ESP_LOGI(TAG, "倾听等待超时 (3.5s 内未检测到说话声音)");
                break;
            }
        }
    }

    AudioService::getInstance().stopAudioStream();

    // 5. 发送 finish-task 通知云端结束推流
    cJSON *finish_root = cJSON_CreateObject();
    cJSON *f_header = cJSON_CreateObject();
    cJSON_AddStringToObject(f_header, "action", "finish-task");
    cJSON_AddStringToObject(f_header, "task_id", task_id);
    cJSON_AddStringToObject(f_header, "streaming", "duplex");
    cJSON_AddItemToObject(finish_root, "header", f_header);

    cJSON *f_payload = cJSON_CreateObject();
    cJSON *f_input = cJSON_CreateObject();
    cJSON_AddItemToObject(f_payload, "input", f_input);
    cJSON_AddItemToObject(finish_root, "payload", f_payload);

    char *finish_json = cJSON_PrintUnformatted(finish_root);
    cJSON_Delete(finish_root);

    if (finish_json) {
        esp_websocket_client_send_text(ws_client, finish_json, strlen(finish_json), portMAX_DELAY);
        cJSON_free(finish_json);
    }

    // 6. 等待服务端返回 task-finished 最终结果 (超时 1.5 秒快速回包)
    int wait_finish_ms = 0;
    while (!ctx.is_finished.load() && wait_finish_ms < 1500) {
        vTaskDelay(pdMS_TO_TICKS(40));
        wait_finish_ms += 40;
    }

    // 7. 优雅关闭并释放资源
    esp_websocket_client_stop(ws_client);
    esp_websocket_client_destroy(ws_client);

    // 清理首尾空白字符
    std::string result = ctx.recognized_text;
    while (!result.empty() && (result.front() == ' ' || result.front() == '\t' || result.front() == '\r' || result.front() == '\n')) {
        result.erase(0, 1);
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '\t' || result.back() == '\r' || result.back() == '\n')) {
        result.pop_back();
    }

    return result;
}

void AiService::processTextInteraction(const std::string &prompt) {
    ESP_LOGI(TAG, "收到对话提示词: %s", prompt.c_str());
    WebServer::getInstance().broadcastAiStatus("thinking");
    DisplayService::getInstance().setEmotion(EmotionState::THINKING);

    // 快捷重置对话记忆逻辑
    if (prompt == "重置对话" || prompt == "清空记忆" || prompt == "忘掉刚才") {
        history_.clear();
        std::string reply = "好的，所有对话记忆已清空，我们重新开始吧！[ACTION:HAPPY]";
        std::string clean = dispatchActions(reply);
        WebServer::getInstance().broadcastAiReply(clean);
        WebServer::getInstance().broadcastAiStatus("speaking");
        streamTts(clean);
        return;
    }

    // 1. 调用大模型推理
    std::string raw_reply = callLlm(prompt);
    if (raw_reply.empty()) {
        raw_reply = "抱歉，连接云端大模型失败，请检查网络或配置。[ACTION:CONFUSED]";
    }

    // 2. 解析动作标签并提取纯朗读文本 (自动去除 emoji 和动作标签)
    std::string clean_text = dispatchActions(raw_reply);
    ESP_LOGI(TAG, "大模型回答: %s", clean_text.c_str());

    // 3. 记录多轮对话上下文 (维持最大 8 条历史记录 = 4 轮问答)
    if (!clean_text.empty()) {
        history_.push_back({"user", prompt});
        history_.push_back({"assistant", clean_text});
        if (history_.size() > 8) {
            history_.erase(history_.begin(), history_.begin() + 2);
        }
    }

    // 4. 推送文本回复到前端控制台
    WebServer::getInstance().broadcastAiReply(clean_text);
    WebServer::getInstance().broadcastAiStatus("speaking");

    // 5. 调用 CosyVoice 语音流式合成与朗读
    streamTts(clean_text);
}

std::string AiService::callLlm(const std::string &user_prompt) {
    std::string result_text;

    // 1. 构建当前现实时间环境上下文 (由 SNTP 自动同步)
    time_t now_sec = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now_sec, &timeinfo);
    char time_str[128] = {0};
    if (timeinfo.tm_year > 120) { // 已获取到 2020 年以后的真实北京时间
        const char *w_str = "日";
        switch (timeinfo.tm_wday) {
            case 1: w_str = "一"; break;
            case 2: w_str = "二"; break;
            case 3: w_str = "三"; break;
            case 4: w_str = "四"; break;
            case 5: w_str = "五"; break;
            case 6: w_str = "六"; break;
            default: w_str = "日"; break;
        }
        snprintf(time_str, sizeof(time_str), "\n【现实世界环境】当前北京时间：%04d年%02d月%02d日 星期%s %02d:%02d。",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 w_str, timeinfo.tm_hour, timeinfo.tm_min);
    }
    std::string full_sys_prompt = std::string(Config::CloudAI::SYSTEM_PROMPT) + time_str;

    // 2. 构建 OpenAI 兼容的请求体 JSON (包含历史上下文与最新问题)
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", Config::CloudAI::LLM_MODEL);

    cJSON *messages = cJSON_CreateArray();

    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", full_sys_prompt.c_str());
    cJSON_AddItemToArray(messages, sys_msg);

    // 注入历史多轮对话上下文 (支持连续问答、语义承接)
    for (const auto &item : history_) {
        cJSON *h_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(h_msg, "role", item.role.c_str());
        cJSON_AddStringToObject(h_msg, "content", item.content.c_str());
        cJSON_AddItemToArray(messages, h_msg);
    }

    cJSON *usr_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(usr_msg, "role", "user");
    cJSON_AddStringToObject(usr_msg, "content", user_prompt.c_str());
    cJSON_AddItemToArray(messages, usr_msg);

    cJSON_AddItemToObject(root, "messages", messages);
    bool needs_search = (user_prompt.find("天气") != std::string::npos ||
                         user_prompt.find("新闻") != std::string::npos ||
                         user_prompt.find("股市") != std::string::npos ||
                         user_prompt.find("查一下") != std::string::npos ||
                         user_prompt.find("搜索") != std::string::npos);
    cJSON_AddBoolToObject(root, "enable_search", needs_search);
    cJSON_AddNumberToObject(root, "max_tokens", 100);

    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!post_data) return result_text;

    // 2. 配置 HTTPS 客户端并绑定数据接收回调
    std::string response_body;

    esp_http_client_config_t http_cfg = {};
    http_cfg.url = Config::CloudAI::LLM_ENDPOINT;
    http_cfg.method = HTTP_METHOD_POST;
    http_cfg.timeout_ms = 15000;
    http_cfg.event_handler = llmHttpEventHandler;
    http_cfg.user_data = &response_body;
    http_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    http_cfg.buffer_size = 4096;
    http_cfg.buffer_size_tx = 2048;

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        cJSON_free(post_data);
        return result_text;
    }

    std::string auth_header = std::string("Bearer ") + Config::CloudAI::API_KEY;
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", auth_header.c_str());
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    ESP_LOGI(TAG, "正在向阿里云百炼大模型发起请求: %s", Config::CloudAI::LLM_ENDPOINT);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "LLM HTTP 响应码: %d, 响应数据大小: %u 字节", status_code,
                 static_cast<unsigned>(response_body.size()));

        if (status_code == 200 && !response_body.empty()) {
            cJSON *resp_json = cJSON_Parse(response_body.c_str());
            if (resp_json) {
                cJSON *choices = cJSON_GetObjectItem(resp_json, "choices");
                if (choices && cJSON_GetArraySize(choices) > 0) {
                    cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
                    cJSON *msg_obj = cJSON_GetObjectItem(first_choice, "message");
                    if (msg_obj) {
                        cJSON *content = cJSON_GetObjectItem(msg_obj, "content");
                        if (content && cJSON_IsString(content)) {
                            result_text = content->valuestring;
                        }
                    }
                }
                cJSON_Delete(resp_json);
            } else {
                ESP_LOGE(TAG, "解析 LLM JSON 失败，原始内容: %s", response_body.c_str());
            }
        } else {
            ESP_LOGE(TAG, "LLM 返回非 200 状态码: %d, 响应: %s", status_code, response_body.c_str());
        }
    } else {
        ESP_LOGE(TAG, "LLM HTTP 请求失败: %s (代码: %d)", esp_err_to_name(err), err);
    }

    esp_http_client_cleanup(client);
    cJSON_free(post_data);
    return result_text;
}

std::string AiService::dispatchActions(const std::string &reply_raw) {
    std::string text = reply_raw;

    // 1. 解析大模型生成的情感表达标签，驱动屏幕呈现对应赛博表情
    if (text.find("[EMOTION:HAPPY]") != std::string::npos || text.find("[EMOTION:SMILE]") != std::string::npos) {
        DisplayService::getInstance().setEmotion(EmotionState::HAPPY);
    } else if (text.find("[EMOTION:SURPRISED]") != std::string::npos) {
        DisplayService::getInstance().setEmotion(EmotionState::SURPRISED);
    } else if (text.find("[EMOTION:CONFUSED]") != std::string::npos) {
        DisplayService::getInstance().setEmotion(EmotionState::CONFUSED);
    } else if (text.find("[EMOTION:LOVE]") != std::string::npos || text.find("[EMOTION:HEART]") != std::string::npos) {
        DisplayService::getInstance().setEmotion(EmotionState::LOVE);
    } else if (text.find("[EMOTION:COOL]") != std::string::npos) {
        DisplayService::getInstance().setEmotion(EmotionState::COOL);
    } else if (text.find("[EMOTION:WINK]") != std::string::npos) {
        DisplayService::getInstance().setEmotion(EmotionState::WINK);
    } else if (text.find("[EMOTION:ANGRY]") != std::string::npos) {
        DisplayService::getInstance().setEmotion(EmotionState::ANGRY);
    } else if (text.find("[EMOTION:SLEEPY]") != std::string::npos) {
        DisplayService::getInstance().setEmotion(EmotionState::SLEEPY);
    }

    // 2. 解析常见的拟人实体动作
    if (text.find("[ACTION:NOD]") != std::string::npos) {
        InteractionService::getInstance().triggerBehavior(RobotBehavior::NOD);
    }
    if (text.find("[ACTION:SHAKE]") != std::string::npos) {
        InteractionService::getInstance().triggerBehavior(RobotBehavior::SHAKE);
    }
    if (text.find("[ACTION:HAPPY]") != std::string::npos || text.find("[ACTION:SMILE]") != std::string::npos) {
        InteractionService::getInstance().triggerBehavior(RobotBehavior::HAPPY);
    }
    if (text.find("[ACTION:DANCE]") != std::string::npos) {
        InteractionService::getInstance().triggerBehavior(RobotBehavior::DANCE);
    }
    if (text.find("[ACTION:SURPRISED]") != std::string::npos) {
        InteractionService::getInstance().triggerBehavior(RobotBehavior::WAKE_UP);
    }
    if (text.find("[ACTION:CONFUSED]") != std::string::npos) {
        InteractionService::getInstance().triggerBehavior(RobotBehavior::CONFUSED);
    }
    if (text.find("[ACTION:FORWARD]") != std::string::npos) {
        ChassisService::getInstance().sendVelocityCommand(60, 0);
        // 延时后自动制动
        xTaskCreate([](void *) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ChassisService::getInstance().sendVelocityCommand(0, 0);
            vTaskDelete(nullptr);
        }, "ChassisBriefMove", 2048, nullptr, 5, nullptr);
    }
    if (text.find("[ACTION:BACKWARD]") != std::string::npos) {
        ChassisService::getInstance().sendVelocityCommand(-60, 0);
        xTaskCreate([](void *) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ChassisService::getInstance().sendVelocityCommand(0, 0);
            vTaskDelete(nullptr);
        }, "ChassisBriefMove", 2048, nullptr, 5, nullptr);
    }
    if (text.find("[ACTION:SPIN]") != std::string::npos) {
        ChassisService::getInstance().sendVelocityCommand(0, 300);
        xTaskCreate([](void *) {
            vTaskDelay(pdMS_TO_TICKS(1200));
            ChassisService::getInstance().sendVelocityCommand(0, 0);
            vTaskDelete(nullptr);
        }, "ChassisBriefMove", 2048, nullptr, 5, nullptr);
    }

    // 剔除所有 [ACTION:XXX] 与 [EMOTION:XXX] 标签
    std::regex tag_regex("\\[(ACTION|EMOTION):[A-Z_]+\\]");
    std::string clean = std::regex_replace(text, tag_regex, "");

    // 剔除所有 emoji 与装饰符号
    return stripEmojisAndIcons(clean);
}

void AiService::playBootVoiceGreeting() {
    is_busy_.store(true);
    ESP_LOGI(TAG, "Wi-Fi 连接成功，触发机器人开机语音问候...");
    std::string greeting = "系统启动完毕，小智同学已就绪，随时听候差遣！[EMOTION:HAPPY][ACTION:NOD]";
    std::string clean = dispatchActions(greeting);
    WebServer::getInstance().broadcastAiReply(clean);
    WebServer::getInstance().broadcastAiStatus("speaking");
    streamTts(clean);
    WebServer::getInstance().broadcastAiStatus("idle");
    is_busy_.store(false);
}

static void ttsWebsocketEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    auto *ctx = static_cast<TtsSessionContext *>(handler_args);
    auto *data = static_cast<esp_websocket_event_data_t *>(event_data);

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "CosyVoice WebSocket 连接握手成功！");
            ctx->is_connected.store(true);
            break;

        case WEBSOCKET_EVENT_DATA:
            // 二进制音频流帧 (op_code 0x02 或 0x00 续帧): 16kHz 16-Bit Mono PCM
            if (data->op_code == 0x02 || data->op_code == 0x00) {
                if (data->data_len > 0) {
                    ctx->total_bytes_received += data->data_len;
                    const int16_t *pcm_samples = reinterpret_cast<const int16_t *>(data->data_ptr);
                    size_t sample_count = data->data_len / sizeof(int16_t);
                    AudioService::getInstance().playStreamPCM(pcm_samples, sample_count);
                }
            } else if (data->op_code == 0x01) {
                // 文本控制帧，解析事件
                if (data->data_ptr && data->data_len > 0) {
                    std::string text_msg(data->data_ptr, data->data_len);
                    if (text_msg.find("task-finished") != std::string::npos) {
                        ESP_LOGI(TAG, "CosyVoice 任务完成通知！共接收 %u 字节音频", (unsigned)ctx->total_bytes_received);
                        ctx->is_finished.store(true);
                    } else if (text_msg.find("task-failed") != std::string::npos) {
                        ESP_LOGE(TAG, "CosyVoice 任务报告失败: %s", text_msg.c_str());
                        ctx->has_error.store(true);
                        ctx->is_finished.store(true);
                    }
                }
            }
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "CosyVoice WebSocket 会话连接断开");
            ctx->is_finished.store(true);
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "CosyVoice WebSocket 发生错误");
            ctx->has_error.store(true);
            ctx->is_finished.store(true);
            break;

        default:
            break;
    }
}

void AiService::streamTts(const std::string &text_to_speak) {
    if (text_to_speak.empty()) return;

    ESP_LOGI(TAG, "正在启动 CosyVoice 语音合成: \"%s\"", text_to_speak.c_str());
    AudioService::getInstance().setPlaybackActive(true);

    TtsSessionContext ctx;
    std::string auth_header = std::string("Authorization: Bearer ") + Config::CloudAI::API_KEY + "\r\n";

    esp_websocket_client_config_t ws_cfg = {};
    ws_cfg.uri = Config::CloudAI::TTS_WS_ENDPOINT;
    ws_cfg.headers = auth_header.c_str();
    ws_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    ws_cfg.buffer_size = 16384;
    ws_cfg.network_timeout_ms = 10000;

    esp_websocket_client_handle_t ws_client = esp_websocket_client_init(&ws_cfg);
    if (!ws_client) {
        ESP_LOGE(TAG, "无法创建 WebSocket 客户端实例！");
        AudioService::getInstance().setPlaybackActive(false);
        return;
    }

    esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, ttsWebsocketEventHandler, &ctx);

    esp_err_t ret = esp_websocket_client_start(ws_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "无法启动 WebSocket 客户端: %s", esp_err_to_name(ret));
        esp_websocket_client_destroy(ws_client);
        AudioService::getInstance().setPlaybackActive(false);
        return;
    }

    // 1. 等待 WebSocket 连接成功 (超时 6 秒)
    int wait_conn_ms = 0;
    while (!ctx.is_connected.load() && !ctx.has_error.load() && wait_conn_ms < 6000) {
        vTaskDelay(pdMS_TO_TICKS(50));
        wait_conn_ms += 50;
    }

    if (!ctx.is_connected.load()) {
        ESP_LOGE(TAG, "连接 CosyVoice WebSocket 超时！");
        esp_websocket_client_stop(ws_client);
        esp_websocket_client_destroy(ws_client);
        AudioService::getInstance().setPlaybackActive(false);
        return;
    }

    // 2. 生成唯一 task_id 并发送 run-task 指令
    char task_id[32];
    snprintf(task_id, sizeof(task_id), "task_%llu", static_cast<unsigned long long>(esp_timer_get_time()));

    cJSON *run_root = cJSON_CreateObject();
    cJSON *header = cJSON_CreateObject();
    cJSON_AddStringToObject(header, "action", "run-task");
    cJSON_AddStringToObject(header, "task_id", task_id);
    cJSON_AddStringToObject(header, "streaming", "duplex");
    cJSON_AddItemToObject(run_root, "header", header);

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "task_group", "audio");
    cJSON_AddStringToObject(payload, "task", "tts");
    cJSON_AddStringToObject(payload, "function", "SpeechSynthesizer");
    cJSON_AddStringToObject(payload, "model", Config::CloudAI::TTS_MODEL);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "text_type", "PlainText");
    cJSON_AddStringToObject(params, "voice", Config::CloudAI::TTS_VOICE);
    cJSON_AddStringToObject(params, "format", "pcm");
    cJSON_AddNumberToObject(params, "sample_rate", Config::CloudAI::TTS_SAMPLE_RATE);
    cJSON_AddItemToObject(payload, "parameters", params);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "text", text_to_speak.c_str());
    cJSON_AddItemToObject(payload, "input", input);

    cJSON_AddItemToObject(run_root, "payload", payload);

    char *run_json = cJSON_PrintUnformatted(run_root);
    cJSON_Delete(run_root);

    if (run_json) {
        esp_websocket_client_send_text(ws_client, run_json, strlen(run_json), portMAX_DELAY);
        cJSON_free(run_json);
    }

    // 3. 发送 finish-task 通知服务端文本已结束
    cJSON *finish_root = cJSON_CreateObject();
    cJSON *f_header = cJSON_CreateObject();
    cJSON_AddStringToObject(f_header, "action", "finish-task");
    cJSON_AddStringToObject(f_header, "task_id", task_id);
    cJSON_AddStringToObject(f_header, "streaming", "duplex");
    cJSON_AddItemToObject(finish_root, "header", f_header);

    cJSON *f_payload = cJSON_CreateObject();
    cJSON *f_input = cJSON_CreateObject();
    cJSON_AddItemToObject(f_payload, "input", f_input);
    cJSON_AddItemToObject(finish_root, "payload", f_payload);

    char *finish_json = cJSON_PrintUnformatted(finish_root);
    cJSON_Delete(finish_root);

    if (finish_json) {
        esp_websocket_client_send_text(ws_client, finish_json, strlen(finish_json), portMAX_DELAY);
        cJSON_free(finish_json);
    }

    // 4. 等待音频流传输并播放完毕 (超时 15 秒)
    int wait_play_ms = 0;
    while (!ctx.is_finished.load() && wait_play_ms < 15000) {
        vTaskDelay(pdMS_TO_TICKS(50));
        wait_play_ms += 50;
    }

    // 5. 优雅关闭并释放资源
    esp_websocket_client_stop(ws_client);
    esp_websocket_client_destroy(ws_client);
    AudioService::getInstance().setPlaybackActive(false);

    ESP_LOGI(TAG, "CosyVoice 语音流会话结束，总接收 %u 字节 PCM", (unsigned)ctx.total_bytes_received);
}
