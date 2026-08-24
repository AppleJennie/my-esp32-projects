#include "sleep_monitor_api.h"
#include "microphone_manager.h"
#include "microphone_driver.h"
#include "feature_extractor.h"
#include "model_interface.h"
#include "app_config.h"
#include "sleep_data_center.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <string.h>
#include <math.h>

static const char* TAG = "SLEEP_MON";

// ============================================================================
// 局部状态（不依赖 main.cpp 里的全局变量，方便集成到小智工程）
// ============================================================================
static bool s_initialized = false;
static bool s_running = false;
static TaskHandle_t s_audio_task_hdl = nullptr;
static TaskHandle_t s_infer_task_hdl = nullptr;
static QueueHandle_t s_audio_queue = nullptr;

static struct {
    int snore_count;
    float snore_probability;
    float quality_score;
    uint32_t start_ms;
    uint32_t last_update_ms;
} s_state;

// 音频缓冲（PSRAM 动态分配，避免占用内部 DRAM）
static int16_t* s_audio_buffer = nullptr;
#define AUDIO_BUFFER_TOTAL_SAMPLES (AUDIO_CHUNK_SAMPLES * AUDIO_MFCCFrames_PER_INF)

// ============================================================================
// 内部辅助函数
// ============================================================================
static inline float db_from_rms(const int16_t* samples, int count) {
    float sum_sq = 0.0f;
    for (int i = 0; i < count; i++) {
        float s = samples[i] / 32768.0f;
        sum_sq += s * s;
    }
    float rms = sqrtf(sum_sq / count);
    return 20.0f * log10f(rms + 1e-10f) + 94.0f;
}

static void update_snore_state(float prob, uint32_t ts_ms) {
    static bool in_event = false;
    static int consec_snore = 0;
    static int consec_non = 0;

    s_state.snore_probability = prob;

    if (prob > 0.6f) {
        consec_snore++;
        consec_non = 0;
    } else {
        consec_non++;
        consec_snore = 0;
    }

    if (consec_snore >= 2 && !in_event) {
        in_event = true;
        s_state.snore_count++;
        ESP_LOGW(TAG, "Snore event #%d started! prob=%.2f", s_state.snore_count, prob);
    }
    if (consec_non >= 3 && in_event) {
        in_event = false;
        ESP_LOGI(TAG, "Snore event ended");
    }

    // 简化睡眠质量评分（基于鼾声频率）
    float elapsed_h = (ts_ms - s_state.start_ms) / 3600000.0f;
    if (elapsed_h > 0.05f) {
        float snore_per_h = s_state.snore_count / elapsed_h;
        s_state.quality_score = 100.0f - (snore_per_h * 100.0f / 60.0f);
        if (s_state.quality_score < 0.0f) s_state.quality_score = 0.0f;
        if (s_state.quality_score > 100.0f) s_state.quality_score = 100.0f;
    }

    s_state.last_update_ms = ts_ms;
}

// ============================================================================
// FreeRTOS 任务
// ============================================================================
static void sm_audio_task(void* pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "Audio task started on core %d", xPortGetCoreID());

    AudioChunk chunk;
    while (s_running) {
        size_t bytes_read = 0;
        esp_err_t err = microphone_read(chunk.samples, AUDIO_CHUNK_BYTES, &bytes_read);
        if (err == ESP_OK && bytes_read == AUDIO_CHUNK_BYTES) {
            chunk.timestamp_ms = esp_timer_get_time() / 1000;
            chunk.rms_db = db_from_rms(chunk.samples, AUDIO_CHUNK_SAMPLES);

            // 非阻塞入队，队列满则丢弃最旧数据
            if (xQueueSend(s_audio_queue, &chunk, 0) != pdPASS) {
                AudioChunk discard;
                xQueueReceive(s_audio_queue, &discard, 0);
                xQueueSend(s_audio_queue, &chunk, 0);
            }
        }
        // 每 100ms 生成一帧，模拟真实麦克风采样节奏，同时让出 CPU
        vTaskDelay(pdMS_TO_TICKS(AUDIO_BUFFER_MS));
    }

    ESP_LOGI(TAG, "Audio task exiting");
    s_audio_task_hdl = nullptr;
    vTaskDelete(NULL);
}

static void sm_inference_task(void* pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "Inference task started on core %d", xPortGetCoreID());

    float features[AUDIO_FEATURE_SIZE];
    float output[1];
    AudioChunk chunk;
    int buf_idx = 0;
    TickType_t last_infer = xTaskGetTickCount();

    while (s_running) {
        // 收集音频
        while (buf_idx < AUDIO_MFCCFrames_PER_INF &&
               xQueueReceive(s_audio_queue, &chunk, pdMS_TO_TICKS(50)) == pdPASS) {
            memcpy(&s_audio_buffer[buf_idx * AUDIO_CHUNK_SAMPLES],
                   chunk.samples, AUDIO_CHUNK_BYTES);
            buf_idx++;
        }

        // 每 3 秒且缓冲满时推理一次
        if (buf_idx >= AUDIO_MFCCFrames_PER_INF &&
            xTaskGetTickCount() - last_infer >= pdMS_TO_TICKS(3000)) {
            last_infer = xTaskGetTickCount();
            uint32_t t0 = esp_timer_get_time() / 1000;

            extract_audio_features(s_audio_buffer,
                                   buf_idx * AUDIO_CHUNK_SAMPLES,
                                   features);

            if (model_interface_invoke(features, output)) {
                uint32_t ts = esp_timer_get_time() / 1000;
                update_snore_state(output[0], ts);

                // 更新 SleepDataCenter 统计仓库
                SleepDataCenter::GetInstance().UpdateSnore(
                    output[0],
                    output[0] > 0.70f,
                    3
                );

                ESP_LOGI(TAG, "Snore prob=%.3f | events=%d | quality=%.0f | infer=%lums",
                         output[0], s_state.snore_count, s_state.quality_score,
                         (unsigned long)(ts - t0));
            } else {
                ESP_LOGE(TAG, "Model inference failed");
            }

            // 滑动窗口 50% 重叠
            int overlap = AUDIO_MFCCFrames_PER_INF / 2;
            memmove(s_audio_buffer,
                    &s_audio_buffer[overlap * AUDIO_CHUNK_SAMPLES],
                    overlap * AUDIO_CHUNK_BYTES);
            buf_idx = overlap;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Inference task exiting");
    s_infer_task_hdl = nullptr;
    vTaskDelete(NULL);
}

// ============================================================================
// 对外 API 实现
// ============================================================================
bool sleep_monitor_init(void) {
    if (s_initialized) {
        return true;
    }

    if (!feature_extractor_init()) {
        ESP_LOGE(TAG, "Feature extractor init failed");
        return false;
    }
    if (!model_interface_init()) {
        ESP_LOGE(TAG, "Model interface init failed");
        return false;
    }

    // 音频缓冲分配在 PSRAM，避免 dram0_0_seg 溢出
    if (!s_audio_buffer) {
        s_audio_buffer = (int16_t*)heap_caps_malloc(
            AUDIO_BUFFER_TOTAL_SAMPLES * sizeof(int16_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_audio_buffer) {
            ESP_LOGE(TAG, "Failed to allocate audio buffer in PSRAM");
            return false;
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Sleep monitor initialized");
    return true;
}

bool sleep_monitor_start(void) {
    // 兜底保护：未初始化则自动尝试初始化
    if (!s_initialized) {
        ESP_LOGW(TAG, "Not initialized, calling sleep_monitor_init() now");
        if (!sleep_monitor_init()) {
            ESP_LOGE(TAG, "Auto-init failed, cannot start sleep monitor");
            return false;
        }
    }
    if (s_running) {
        ESP_LOGI(TAG, "Sleep monitor already running");
        return true;
    }

    // 重置状态
    memset(&s_state, 0, sizeof(s_state));
    s_state.start_ms = esp_timer_get_time() / 1000;

    // 初始化麦克风
    if (!microphone_manager_init()) {
        return false;
    }

    // 创建队列
    s_audio_queue = xQueueCreate(AUDIO_QUEUE_LENGTH, sizeof(AudioChunk));
    if (!s_audio_queue) {
        microphone_manager_deinit();
        ESP_LOGE(TAG, "Failed to create audio queue");
        return false;
    }

    s_running = true;

    BaseType_t r1 = xTaskCreatePinnedToCore(
        sm_audio_task, "sm_audio", AUDIO_TASK_STACK, NULL,
        AUDIO_TASK_PRIORITY, &s_audio_task_hdl, 0);

    BaseType_t r2 = xTaskCreatePinnedToCore(
        sm_inference_task, "sm_infer", INFERENCE_TASK_STACK, NULL,
        INFERENCE_TASK_PRIORITY, &s_infer_task_hdl, 1);

    if (r1 != pdPASS || r2 != pdPASS) {
        ESP_LOGE(TAG, "Failed to create tasks");
        sleep_monitor_stop();
        return false;
    }

    ESP_LOGI(TAG, "Sleep monitor started");
    return true;
}

void sleep_monitor_stop(void) {
    if (!s_running) {
        return;
    }

    s_running = false;

    // 停止麦克风驱动，audio task 检测到 s_running=false 后自行退出
    microphone_manager_deinit();

    // 等待任务自行退出（最多等 500ms）
    int wait_ms = 0;
    while ((s_audio_task_hdl || s_infer_task_hdl) && wait_ms < 500) {
        vTaskDelay(pdMS_TO_TICKS(50));
        wait_ms += 50;
    }

    if (s_audio_queue) {
        vQueueDelete(s_audio_queue);
        s_audio_queue = nullptr;
    }

    s_audio_task_hdl = nullptr;
    s_infer_task_hdl = nullptr;

    ESP_LOGI(TAG, "Sleep monitor stopped. Total snore events: %d", s_state.snore_count);
}

bool sleep_monitor_is_running(void) {
    return s_running;
}

int sleep_monitor_get_snore_count(void) {
    return s_state.snore_count;
}

float sleep_monitor_get_snore_probability(void) {
    return s_state.snore_probability;
}

float sleep_monitor_get_quality_score(void) {
    return s_state.quality_score;
}

void sleep_monitor_get_report(char* buf, size_t len) {
    uint32_t elapsed_s = (s_state.last_update_ms - s_state.start_ms) / 1000;
    float elapsed_h = elapsed_s / 3600.0f;

    snprintf(buf, len,
        "=== Sleep Report ===\n"
        "Duration:    %02lu:%02lu:%02lu\n"
        "SnoreEvents: %d\n"
        "SnoreRate:   %.1f/h\n"
        "Quality:     %.0f/100\n"
        "===================",
        elapsed_s / 3600, (elapsed_s % 3600) / 60, elapsed_s % 60,
        s_state.snore_count,
        elapsed_h > 0.01f ? s_state.snore_count / elapsed_h : 0.0f,
        s_state.quality_score
    );
}
