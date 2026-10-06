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

// ============================================================================
// 【UTF-8 字符流过滤算法：彻底剔除 Emoji 表情与图形符号】
//
// 嵌入式 ST7735 彩屏字库采用轻量级 ASCII 与 GB2312 点阵字模，内存中并未存储庞大的 Emoji
// 字形。若大模型输出包含 4 字节 Emoji（如笑脸、爱心、小手等），点阵渲染器会解析失败并显示
// 为乱码黑块或问号。本函数根据 RFC 3629 UTF-8 变长编码规范在 O(N) 线性时间内执行单遍扫描：
//  - 1 字节 (ASCII 字符): [0x00, 0x7F]，直接保留；
//  - 2 字节 (拉丁文/希腊文等): [0xC0..0xDF]，直接保留；
//  - 3 字节 (中文字符与基础符号): [0xE0..0xEF]，保留常规汉字，精准过滤装饰符号 (0xE2 90~BF) 与变体选择器 (0xEF B8)；
//  - 4 字节 (辅助平面/全量标准 Emoji): [0xF0..0xF7]，直接跳过丢弃。
// ============================================================================
static std::string stripEmojisAndIcons(const std::string &str) {
    std::string clean;
    clean.reserve(str.size());
    size_t i = 0;
    while (i < str.size()) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if (c < 0x80) {
            // 1 字节标准 ASCII
            clean += str[i++];
        } else if ((c & 0xE0) == 0xC0) {
            // 2 字节 UTF-8 字符
            if (i + 1 < str.size()) {
                clean += str[i];
                clean += str[i + 1];
                i += 2;
            } else {
                break;
            }
        } else if ((c & 0xF0) == 0xE0) {
            // 3 字节 UTF-8: 过滤特殊杂项符号 (Miscellaneous Symbols) 与变体选择符 (Variation Selectors)
            if (i + 2 < str.size()) {
                unsigned char c2 = static_cast<unsigned char>(str[i + 1]);
                if (c == 0xE2 && (c2 >= 0x90 && c2 <= 0xBF)) {
                    i += 3; // 丢弃装饰字符
                } else if (c == 0xEF && c2 == 0xB8) {
                    i += 3; // 丢弃 Emoji 变体修饰符
                } else {
                    // 保留标准 3 字节常用汉字 (U+4E00 ~ U+9FFF)
                    clean += str[i];
                    clean += str[i + 1];
                    clean += str[i + 2];
                    i += 3;
                }
            } else {
                break;
            }
        } else if ((c & 0xF8) == 0xF0) {
            // 4 字节 UTF-8: 对应 Unicode U+10000 以上的辅助平面（绝大多数彩色 Emoji 均在此区段）
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

// ============================================================================
// 【CosyVoice TTS 实时语音合成会话状态上下文】
// ============================================================================
// CosyVoice 采用全双工 WebSocket 长连接：
//  - 客户端上送：JSON 控制指令 (run-task, finish-task)；
//  - 服务端下行：纯二进制音频流 (op_code 0x02 或 0x00 连续续帧)，携带 16kHz 16-Bit Mono PCM 裸音频，
//               收到后直接推入机载 I2S 硬件 DAC 驱动发声。
struct TtsSessionContext {
    std::atomic<bool> is_connected{false};      // WebSocket TLS 握手连通
    std::atomic<bool> is_finished{false};       // 服务端播报结束通知 (task-finished)
    std::atomic<bool> has_error{false};          // 传输或模型生成异常 (task-failed)
    size_t total_bytes_received{0};             // 接收到的 PCM 音频总字节数
};

// ============================================================================
// 【DashScope Paraformer ASR 实时语音识别会话上下文】
// ============================================================================
// 实时语音识别采用流式全双工 WebSocket：
//  - 客户端上行：向云端连续推送 16kHz 16-Bit 单声道二进制 PCM 帧 (op_code 0x02)；
//  - 服务端下行：向客户端实时返回 JSON 文本帧 (op_code 0x01)，下发递增的识别结果。
struct AsrSessionContext {
    std::atomic<bool> is_connected{false};          // WebSocket TLS 握手连通
    std::atomic<bool> is_started{false};            // 云端确认 task-started，已开启动态识别引擎
    std::atomic<bool> is_finished{false};           // 识别任务完整结束
    std::atomic<bool> has_error{false};              // 链路错误标志
    std::atomic<bool> sentence_end_detected{false}; // 云端 VAD 判定当前自然分句已完整结束

    // 【多句动态索引映射】：
    // Paraformer 采用流式多句分割机制，每个句子拥有独立的 sentence_id：
    // 在用户发音过程中，同一个 sentence_id 的文本会逐步递增修正（例如："小" -> "小智" -> "小智同学"）；
    // 当遇到长停顿，云端会生成新的 sentence_id。通过 std::map<int, std::string> 记录，
    // 可以保证每一句的最新修正结果实时生效，最终拼接出零错位的完整用户口述。
    std::map<int, std::string> sentence_map;
    std::string recognized_text;                    // 最终汇总拼接的完整用户口述文本
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

    // 步骤 1: 拟人化即时感知反馈
    // 听到声音立刻向屏幕和舵机分发 WAKE_UP（睁大眼睛、微仰头倾听），给予人类极佳的即时交互反馈
    WebServer::getInstance().broadcastAiStatus("listening");
    InteractionService::getInstance().triggerBehavior(RobotBehavior::WAKE_UP);

    // 步骤 2: 启动流式录音并推流至云端 DashScope ASR
    // 内部通过 64KB PSRAM 环形缓冲区提取 800ms 预缓冲 + 实时音频流
    std::string recognized = recordAndTranscribe();
    if (recognized.empty()) {
        ESP_LOGI(TAG, "未识别到有效语音文本，返回平静常态。");
        InteractionService::getInstance().triggerBehavior(RobotBehavior::NORMAL);
        return;
    }

    ESP_LOGI(TAG, "硬件麦克风语音识别原始文本: \"%s\"", recognized.c_str());

    // 步骤 3: 15 秒多轮对话免唤醒会话管理 (Multi-turn Active Session)
    // 人类在连续对话时不需要每次都喊唤醒词。若上次有效交互距今在 15 秒以内，自动保持唤醒倾听状态。
    uint64_t now_ms = esp_timer_get_time() / 1000ULL;
    constexpr uint64_t ACTIVE_SESSION_TIMEOUT_MS = 15000;
    bool is_in_active_session = (last_interaction_time_ms_ > 0) &&
                                ((now_ms - static_cast<uint64_t>(last_interaction_time_ms_)) < ACTIVE_SESSION_TIMEOUT_MS);

    // 步骤 4: 唤醒词同音词多路模糊匹配
    // Paraformer 针对普通话口音或不同声学环境可能转写为同音字 (如“小智”可能被转写为“小志/小制/肖智”)
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

    // 步骤 5: 单字口癖与环境瞬态脉冲杂音过滤
    // 关门声、咳嗽声、清嗓声经常被 ASR 模型错认成“嗯”、“啊”、“哦”等虚词，直接丢弃避免误触发
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

    // 步骤 6: 口语交互意图宽松判定
    // 包含常见疑问词、指令词或日常问候
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

    // 步骤 7: 提取有效提示词 (裁剪掉唤醒词前缀)
    // 例如：“小智同学，今天天气怎么样？” -> 裁剪为 “今天天气怎么样？” 送入大模型
    std::string effective_prompt = recognized;
    if (has_wake_word) {
        effective_prompt.erase(wake_pos, matched_wake.length());
        // 清理裁剪后遗留的前置标点符号与空白
        while (!effective_prompt.empty()) {
            unsigned char c = static_cast<unsigned char>(effective_prompt.front());
            if (c == ' ' || c == '\t' || c == ',' || c == '?' || c == '!') {
                effective_prompt.erase(0, 1);
            } else if (effective_prompt.rfind("，", 0) == 0 ||
                       effective_prompt.rfind("。", 0) == 0 ||
                       effective_prompt.rfind("！", 0) == 0 ||
                       effective_prompt.rfind("？", 0) == 0 ||
                       effective_prompt.rfind("、", 0) == 0) {
                effective_prompt.erase(0, 3); // 3 字节 UTF-8 中文标点
            } else {
                break;
            }
        }
    }

    // 刷新活跃交互时间戳，激活 15 秒多轮对话窗口
    last_interaction_time_ms_ = now_ms;

    // 将用户语音转写结果以 "user" 角色同步至网页控制台
    WebServer::getInstance().broadcastChatMessage(recognized, "user");

    // 步骤 8: 唤醒快速直答旁路 (Fast-path Zero-shot Response)
    // 若用户仅仅喊了名字（如“小智同学”），有效提示词为空：
    // 无需耗费几百毫秒网络往返请求 LLM，直接秒回就绪问候语，提供极速响应体验
    if (effective_prompt.empty()) {
        ESP_LOGI(TAG, "用户仅唤醒呼叫，极速问候秒回...");
        WebServer::getInstance().broadcastAiStatus("speaking");
        std::string reply = "在呢！请问有什么吩咐？[EMOTION:HAPPY][ACTION:NOD]";
        std::string clean = dispatchActions(reply);
        WebServer::getInstance().broadcastAiReply(clean);
        streamTts(clean);
        return;
    }

    // 步骤 9: 携带纯文本问题，转入大模型推理与语音合成主流程
    processTextInteraction(effective_prompt);
}

std::string AiService::recordAndTranscribe() {
    ESP_LOGI(TAG, "开始建立 ASR WebSocket 会话进行麦克风拾音识别...");

    // ========================================================================
    // 【阶段 1：激活 PSRAM 环形音频流管道，回溯 800ms 预缓冲】
    // ========================================================================
    // 此时硬件麦克风后台采集从未停止。将读指针倒退 800ms，把触发唤醒瞬间乃至之前
    // 已经说出的首字（如“小”、“智”）完整包含在消费流中。
    AudioService::getInstance().startAudioStream(12800);

    AsrSessionContext ctx;
    std::string auth_header = std::string("Authorization: Bearer ") + Config::CloudAI::API_KEY + "\r\n";

    // 配置 ESP-IDF 原生 WebSocket 客户端
    esp_websocket_client_config_t ws_cfg = {};
    ws_cfg.uri = Config::CloudAI::ASR_WS_ENDPOINT;
    ws_cfg.headers = auth_header.c_str();
    ws_cfg.crt_bundle_attach = esp_crt_bundle_attach; // 挂载系统根证书包验证 wss TLS
    ws_cfg.buffer_size = 16384;                       // 16KB 传输缓冲 (位于 PSRAM)
    ws_cfg.network_timeout_ms = 8000;                 // 8秒网络超时

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

    // ========================================================================
    // 【阶段 2：等待 WebSocket TLS 握手连接】
    // ========================================================================
    // 在这 200~400ms 握手等待期间，用户正在持续说话，音频会被 background audioTask
    // 持续写入 PSRAM 环形缓冲区，不会产生任何音频丢失！
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

    // ========================================================================
    // 【阶段 3：发送 run-task 指令开启云端 ASR 识别会话】
    // ========================================================================
    // 协议遵循阿里云百炼实时语音识别规范：
    //  - task_group: audio, task: asr, function: recognition
    //  - model: paraformer-realtime-v2 (百炼最新轻量高识别率流式模型)
    //  - format: pcm, sample_rate: 16000 (与机载 INMP441 麦克风硬件采样率完全对齐)
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
    cJSON_AddNumberToObject(params, "max_sentence_silence", 1400); // 云端静音分句门限
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

    // 等待云端下发 task-started 确认事件
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

    // ========================================================================
    // 【阶段 4：实时连续流式录音与智能端点检测 (VAD) 循环】
    // ========================================================================
    // 每次从环形流读取 1600 个采样点 (1600 / 16000 = 100ms = 3200 字节二进制 PCM)
    // 100ms 分块既保证了网络利用率，又保证了云端极低的前端延迟。
    constexpr size_t LIVE_CHUNK = 1600;
    std::vector<int16_t> live_buf(LIVE_CHUNK);

    int64_t start_time = esp_timer_get_time() / 1000;
    int64_t last_speech_time = start_time;
    bool speech_detected = false;

    // 语音活动检测门限 (SPEECH_THRESHOLD)：静音底噪约 0.02，达到 0.05 判定为有人发声
    constexpr float SPEECH_THRESHOLD = 0.05f;
    constexpr int64_t MAX_RECORD_TIME_MS = 8000; // 单次最长录音 8 秒强制保护

    while (!ctx.has_error.load() && !ctx.is_finished.load()) {
        int64_t now = esp_timer_get_time() / 1000;
        int64_t elapsed = now - start_time;

        if (elapsed > MAX_RECORD_TIME_MS) {
            ESP_LOGI(TAG, "达到单次最大录音时限 (8s)，结束拾音");
            break;
        }

        // 从 PSRAM 环形缓冲区提取连续 PCM 音频
        // 在前几次迭代中，会极速将握手期间积攒的 800ms 预缓冲一口气倒灌给云端；
        // 随后自然平滑过渡至 100ms 实时推流。
        size_t samples_read = 0;
        esp_err_t err = AudioService::getInstance().readAudioStream(live_buf.data(), LIVE_CHUNK, &samples_read, 120);
        if (err != ESP_OK || samples_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(15));
            continue;
        }

        // 计算 RMS 声压能量供 VAD 及网页电平表使用
        double sum_squares = 0.0;
        for (size_t i = 0; i < samples_read; i++) {
            sum_squares += static_cast<double>(live_buf[i]) * static_cast<double>(live_buf[i]);
        }
        double rms = std::sqrt(sum_squares / static_cast<double>(samples_read));
        float energy = std::clamp(static_cast<float>(rms / 6000.0), 0.0f, 1.0f);
        AudioService::getInstance().setCurrentEnergy(energy);

        // 推送二进制音频帧 (Binary PCM Data)
        int sent = esp_websocket_client_send_bin(ws_client,
                                                 reinterpret_cast<const char *>(live_buf.data()),
                                                 samples_read * sizeof(int16_t),
                                                 pdMS_TO_TICKS(200));
        if (sent < 0) {
            ESP_LOGE(TAG, "发送 ASR 音频流失败！");
            break;
        }

        // 语音活动检测 (VAD) 状态机更新
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

            // ================================================================
            // 【三级极速智能断句 (VAD Truncation) 机制】：
            // ================================================================
            // 传统 ASR 往往需要死等 1.5~2.0 秒静音才断句，导致人说完话后机器人发呆很久。
            // 本算法配合云端实时回调，实现超低延迟提前截断：
            //
            // 级别 1 (极速级): 云端声学模型已返回 sentence_end=true（分句完毕），
            //                 且物理静音超过 400ms，且整句说话时长 > 1.2s -> 立即截断！
            if (ctx.sentence_end_detected.load() && silence_duration >= 400 && elapsed >= 1200) {
                ESP_LOGI(TAG, "云端完成断句检测 (sentence_end=true)，提前极速结束拾音");
                break;
            }
            // 级别 2 (自然停顿级): 已经拿到有效字词，且自然停顿超过 700ms -> 立即截断推入大模型！
            if (has_recognized && silence_duration >= 700 && elapsed >= 1200) {
                ESP_LOGI(TAG, "识别到完整语句且自然停顿超过 700ms，极速推入大模型理解");
                break;
            }
            // 级别 3 (保底级): 尚未拿到中间文本但停顿已达 1200ms -> 结束录音
            if (silence_duration >= 1200 && elapsed >= 1500) {
                ESP_LOGI(TAG, "检测到说话停顿超过 1200ms，结束拾音");
                break;
            }
        } else {
            // 开始倾听后连续 3.5 秒始终无人开口发声，超时退出
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

    // ========================================================================
    // 【阶段 1：人机交互状态切换与思考状态反馈】
    // ========================================================================
    // 通知前端 Web 控制台进入 "thinking" 状态，并在 128x128 屏幕上切换为思考表情
    WebServer::getInstance().broadcastAiStatus("thinking");
    DisplayService::getInstance().setEmotion(EmotionState::THINKING);

    // ========================================================================
    // 【阶段 2：特殊指令本地旁路拦截 (快捷重置对话记忆)】
    // ========================================================================
    // 当用户口述清空记忆时，直接本地清空多轮会话队列，无需浪费 Token 请求大模型
    if (prompt == "重置对话" || prompt == "清空记忆" || prompt == "忘掉刚才") {
        history_.clear();
        std::string reply = "好的，所有对话记忆已清空，我们重新开始吧！[ACTION:HAPPY]";
        std::string clean = dispatchActions(reply);
        WebServer::getInstance().broadcastAiReply(clean);
        WebServer::getInstance().broadcastAiStatus("speaking");
        streamTts(clean);
        return;
    }

    // ========================================================================
    // 【阶段 3：调用云端多模态大语言模型 (LLM) 推理】
    // ========================================================================
    // 请求阿里云百炼 Qwen 模型进行语义理解与多模态行为生成
    std::string raw_reply = callLlm(prompt);
    if (raw_reply.empty()) {
        raw_reply = "抱歉，连接云端大模型失败，请检查网络或配置。[ACTION:CONFUSED]";
    }

    // ========================================================================
    // 【阶段 4：解析具身实体行为 (多模态标签调度) 与文本纯净化】
    // ========================================================================
    // 提取 [EMOTION:XXX] 与 [ACTION:XXX] 驱动屏幕表情、舵机点头与底盘动作，
    // 同时过滤掉标签与 Emoji，仅保留自然朗读文本
    std::string clean_text = dispatchActions(raw_reply);
    ESP_LOGI(TAG, "大模型回答: %s", clean_text.c_str());

    // ========================================================================
    // 【阶段 5：多轮对话上下文维护 (Sliding Window FIFO)】
    // ========================================================================
    // 机器人需要具备指代消解与上下文记忆能力（如“它叫什么名字？”能够指代上一句讨论的主题）。
    // 在微控制器有限的 PSRAM/内存环境下，保留最近 8 条消息（4 轮完整问答）：
    // 既能保证连续对话连贯性，又避免 Prompt 过长引发 Token 费用超标和 HTTP 请求包溢出。
    if (!clean_text.empty()) {
        history_.push_back({"user", prompt});
        history_.push_back({"assistant", clean_text});
        if (history_.size() > 8) {
            history_.erase(history_.begin(), history_.begin() + 2); // 弹出最早的一问一答
        }
    }

    // ========================================================================
    // 【阶段 6：推送结果至网页控制台并启动流式语音朗读】
    // ========================================================================
    WebServer::getInstance().broadcastAiReply(clean_text);
    WebServer::getInstance().broadcastAiStatus("speaking");

    // 调用 DashScope CosyVoice 语音流式合成与 I2S 硬件扬声器朗读
    streamTts(clean_text);
}

std::string AiService::callLlm(const std::string &user_prompt) {
    std::string result_text;

    // ========================================================================
    // 【步骤 1：现实世界物理时钟感知注入 (SNTP Context Grounding)】
    // ========================================================================
    // 大语言模型本质上是无状态的静态神经网络权重，本身不知道当前时刻是几点、星期几。
    // 如果直接询问“今天星期几”或“现在几点了”，大模型必然产生严重幻觉。
    // 因此在每次构建 Prompt 时，动态读取机载 SNTP 硬件时钟，将当前北京时间作为物理世界环境事实注入 System Prompt。
    time_t now_sec = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now_sec, &timeinfo);
    char time_str[128] = {0};
    if (timeinfo.tm_year > 120) { // 已通过 NTP 获取到 2020 年以后的真实北京时间
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

    // ========================================================================
    // 【步骤 2：构建符合 OpenAI ChatCompletion 标准规范的 JSON 请求体】
    // ========================================================================
    // 包含 system 提示词、多轮上下文记忆历史与当前最新的 user 问题
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", Config::CloudAI::LLM_MODEL);

    cJSON *messages = cJSON_CreateArray();

    // 注入系统人设与时间上下文
    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", full_sys_prompt.c_str());
    cJSON_AddItemToArray(messages, sys_msg);

    // 注入历史多轮会话记录
    for (const auto &item : history_) {
        cJSON *h_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(h_msg, "role", item.role.c_str());
        cJSON_AddStringToObject(h_msg, "content", item.content.c_str());
        cJSON_AddItemToArray(messages, h_msg);
    }

    // 注入用户当前问题
    cJSON *usr_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(usr_msg, "role", "user");
    cJSON_AddStringToObject(usr_msg, "content", user_prompt.c_str());
    cJSON_AddItemToArray(messages, usr_msg);

    cJSON_AddItemToObject(root, "messages", messages);

    // ========================================================================
    // 【步骤 3：动态联网搜索开关 (Web Search Augmentation)】
    // ========================================================================
    // 当检测到天气、实时新闻、股票或显式搜索意图时，启用百炼大模型动态联网检索功能，
    // 获取当天的最新气象和资讯，避免回答过时的训练集数据。
    bool needs_search = (user_prompt.find("天气") != std::string::npos ||
                         user_prompt.find("新闻") != std::string::npos ||
                         user_prompt.find("股市") != std::string::npos ||
                         user_prompt.find("查一下") != std::string::npos ||
                         user_prompt.find("搜索") != std::string::npos);
    cJSON_AddBoolToObject(root, "enable_search", needs_search);
    cJSON_AddNumberToObject(root, "max_tokens", 100); // 限制回答长度，利于低延迟语音朗读

    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!post_data) return result_text;

    // ========================================================================
    // 【步骤 4：配置 HTTPS 客户端并挂载系统根证书 (CRT Bundle)】
    // ========================================================================
    // 在嵌入式设备上进行 HTTPS 请求时，TLS 握手需校验服务端 SSL 证书。
    // esp_crt_bundle_attach 挂载了 ESP-IDF 内置的 Mozilla 根证书公钥集合，
    // 避免了手动将百炼公钥硬编码烧录到 Flash 的繁琐流程。
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

    // ========================================================================
    // 【步骤 5：执行 HTTP POST 并解析 OpenAI 规范响应 JSON】
    // ========================================================================
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "LLM HTTP 响应码: %d, 响应数据大小: %u 字节", status_code,
                 static_cast<unsigned>(response_body.size()));

        if (status_code == 200 && !response_body.empty()) {
            cJSON *resp_json = cJSON_Parse(response_body.c_str());
            if (resp_json) {
                // 解析 choices[0].message.content 提取大模型生成文本
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

    // ========================================================================
    // 【多模态具身行为调度中枢 (Embodied Multi-Modal Actuation)】
    // ========================================================================
    // 大语言模型输出中包含结构化的实体动作标签，用于控制物理世界中的执行机构：
    //  1. [EMOTION:XXX] -> 屏幕视觉表情呈现 (ST7735 128x128 SPI LCD)
    //  2. [ACTION:XXX]  -> 头部舵机姿态与表情联动 (SG90 双轴云台)
    //  3. [ACTION:FORWARD/BACKWARD/SPIN] -> 底盘轮组物理移动 (直流电机驱动)

    // ------------------------------------------------------------------------
    // 步骤 1：解析视觉情感标签，驱动表情管理服务渲染动态眼眶
    // ------------------------------------------------------------------------
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

    // ------------------------------------------------------------------------
    // 步骤 2：解析肢体动作标签，驱动舵机多自由度拟人微动作 (点头/摇头/雀跃跳舞)
    // ------------------------------------------------------------------------
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

    // ------------------------------------------------------------------------
    // 步骤 3：解析空间位移指令，启动瞬态异步任务执行定时安全自锁巡航
    // ------------------------------------------------------------------------
    // 注意：嵌入式运动控制必须具备“看门狗式定时自锁制动”。
    // 启动一个极轻量的 FreeRTOS 临时任务，在行进 1000~1200ms 后自动执行 sendVelocityCommand(0, 0)
    // 减速刹车并销毁自身 (vTaskDelete(nullptr))，防止机器人失去控制撞击障碍物。
    if (text.find("[ACTION:FORWARD]") != std::string::npos) {
        ChassisService::getInstance().sendVelocityCommand(60, 0);
        xTaskCreate([](void *) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ChassisService::getInstance().sendVelocityCommand(0, 0); // 1秒后自动制动刹停
            vTaskDelete(nullptr); // 任务生命周期结束，释放 TCB 与栈空间
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

    // ------------------------------------------------------------------------
    // 步骤 4：正则表达式剔除所有控制标签，纯净化自然语言文本
    // ------------------------------------------------------------------------
    // 确保语音合成 (TTS) 只朗读自然语音，绝不能把形如 "[ACTION:NOD]" 的控制字符念出来。
    std::regex tag_regex("\\[(ACTION|EMOTION):[A-Z_]+\\]");
    std::string clean = std::regex_replace(text, tag_regex, "");

    // ------------------------------------------------------------------------
    // 步骤 5：字节级过滤 Emoji 与非法高位字符
    // ------------------------------------------------------------------------
    // 保护硬件 LCD 点阵字库，防止非 BMP 区字符引发屏幕乱码崩溃
    return stripEmojisAndIcons(clean);
}

void AiService::playBootVoiceGreeting() {
    is_busy_.store(true);
    ESP_LOGI(TAG, "Wi-Fi 连接成功，触发机器人开机语音问候...");

    // 触发拟人化开机第一声问候，伴随开心表情与点头欢迎动作
    std::string greeting = "系统启动完毕，小智同学已就绪，随时听候差遣！[EMOTION:HAPPY][ACTION:NOD]";
    std::string clean = dispatchActions(greeting);
    WebServer::getInstance().broadcastAiReply(clean);
    WebServer::getInstance().broadcastAiStatus("speaking");
    streamTts(clean);
    WebServer::getInstance().broadcastAiStatus("idle");
    is_busy_.store(false);
}

// ============================================================================
// 【CosyVoice WebSocket 事件回调处理器】
// ============================================================================
// 阿里云百炼 CosyVoice 采用全双工流式传输协议：
//  - 二进制帧 (OpCode 0x02 / 0x00): 服务端实时合成下发的 16kHz 16-bit 单声道 PCM 音频块。
//    收到即写入 I2S DMA 环形缓冲区，实现边下边播（首字发音延迟 < 200ms）。
//  - 文本控制帧 (OpCode 0x01): JSON 格式的状态与控制事件 (task-finished / task-failed)。
static void ttsWebsocketEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    auto *ctx = static_cast<TtsSessionContext *>(handler_args);
    auto *data = static_cast<esp_websocket_event_data_t *>(event_data);

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "CosyVoice WebSocket 连接握手成功！");
            ctx->is_connected.store(true);
            break;

        case WEBSOCKET_EVENT_DATA:
            // ----------------------------------------------------------------
            // 1. 处理二进制音频流数据帧 (Binary PCM Data)
            // ----------------------------------------------------------------
            // OpCode 0x02 (Binary Frame) 或 OpCode 0x00 (Continuation Frame 续帧)
            if (data->op_code == 0x02 || data->op_code == 0x00) {
                if (data->data_len > 0) {
                    ctx->total_bytes_received += data->data_len;
                    const int16_t *pcm_samples = reinterpret_cast<const int16_t *>(data->data_ptr);
                    size_t sample_count = data->data_len / sizeof(int16_t);
                    // 零拷贝直推机载 I2S DMA 环形队列，实现低延迟流式硬件放音
                    AudioService::getInstance().playStreamPCM(pcm_samples, sample_count);
                }
            } else if (data->op_code == 0x01) {
                // ------------------------------------------------------------
                // 2. 处理文本控制帧 (Control JSON Packets)
                // ------------------------------------------------------------
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

    // ========================================================================
    // 【软件回声消除 (Software AEC) 状态锁定】
    // ========================================================================
    // 在扬声器发声前，将 setPlaybackActive 置为 true：
    // 后台麦克风采集任务检测到此标志后，将静音/衰减输入并激活 350ms 混响余震保护期，
    // 彻底杜绝机器人自身音响发出的声音被麦克风重新录入导致“死循环自说自话”。
    AudioService::getInstance().setPlaybackActive(true);

    TtsSessionContext ctx;
    std::string auth_header = std::string("Authorization: Bearer ") + Config::CloudAI::API_KEY + "\r\n";

    // 配置 ESP-IDF 原生 WebSocket 客户端
    esp_websocket_client_config_t ws_cfg = {};
    ws_cfg.uri = Config::CloudAI::TTS_WS_ENDPOINT;
    ws_cfg.headers = auth_header.c_str();
    ws_cfg.crt_bundle_attach = esp_crt_bundle_attach; // 系统根证书链校验
    ws_cfg.buffer_size = 16384;                       // 16KB 传输缓冲
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

    // ========================================================================
    // 【阶段 1：等待 WebSocket TLS 握手连接】
    // ========================================================================
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

    // ========================================================================
    // 【阶段 2：发送 run-task 指令开启语音合成任务】
    // ========================================================================
    // 遵循阿里云百炼 DashScope WebSocket 语音合成交互协议规范：
    //  - model: cosyvoice-v1 (百炼旗舰拟人化音色模型)
    //  - voice: longxiaochun (或配置中指定的专属拟人音色)
    //  - format: pcm, sample_rate: 16000 (与 MAX98357A I2S 硬件完全对齐)
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

    // ========================================================================
    // 【阶段 3：发送 finish-task 指令告知服务端文本输入完毕】
    // ========================================================================
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

    // ========================================================================
    // 【阶段 4：边收边播并等待云端音频流完全下发与播放完成】
    // ========================================================================
    // 音频块在 ttsWebsocketEventHandler 中被实时推进 I2S 硬件，此处主线程等待任务结束
    int wait_play_ms = 0;
    while (!ctx.is_finished.load() && wait_play_ms < 15000) {
        vTaskDelay(pdMS_TO_TICKS(50));
        wait_play_ms += 50;
    }

    // ========================================================================
    // 【阶段 5：会话结束与硬件资源释放】
    // ========================================================================
    esp_websocket_client_stop(ws_client);
    esp_websocket_client_destroy(ws_client);

    // 解除放音保护，进入 350ms 声学混响尾音消除倒计时
    AudioService::getInstance().setPlaybackActive(false);

    ESP_LOGI(TAG, "CosyVoice 语音流会话结束，总接收 %u 字节 PCM", (unsigned)ctx.total_bytes_received);
}
