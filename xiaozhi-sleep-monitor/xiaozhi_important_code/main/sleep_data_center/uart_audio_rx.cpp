#include "uart_audio_rx.h"
#include "sleep_data_center.h"
#include "sleep_sd_logger.h"
#include "feature_extractor.h"
#include "model_interface.h"
#include "sleep_monitor_api.h"
#include "app_config.h"

#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <cstring>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <math.h>

static const char* TAG = "UART_AUDIO";

// ---------------------------------------------------------------------------
// UART 配置
// ---------------------------------------------------------------------------
#define MIXED_UART_NUM      UART_NUM_1
#define MIXED_UART_TX_PIN   GPIO_NUM_17
#define MIXED_UART_RX_PIN   GPIO_NUM_16
#define MIXED_UART_BAUD     921600

// 接收缓冲: 128KB，从 PSRAM 动态分配
#define RX_BUF_SIZE         (1024 * 128)
// 单帧音频 PCM 上限: 90KB
#define MAX_PCM_BYTES       (90 * 1024)
// UART 单次读取缓冲
#define READ_BUF_SIZE       2048
// JSON 单行上限
#define MAX_JSON_LINE       1024
// 音频 AI 队列深度
#define AUDIO_AI_QUEUE_LEN  4
// 推理阈值
#define SNORE_PROB_THRESHOLD  0.75f

// ---------------------------------------------------------------------------
// PSRAM 接收缓冲（运行时分配，禁止放任务栈）
// ---------------------------------------------------------------------------
static uint8_t* s_rx_buf = nullptr;
static int s_rx_pos = 0;

// 统计计数（用于低频汇总打印，避免高频 ESP_LOGI 刷屏）
static uint32_t s_stat_json_count = 0;
static uint32_t s_stat_audio_count = 0;
static uint32_t s_stat_drop_count = 0;
static uint32_t s_stat_last_ms = 0;

// ---------------------------------------------------------------------------
// 音频帧投递结构（队列里只传指针，不拷贝 PCM）
// ---------------------------------------------------------------------------
typedef struct {
    int16_t*  pcm;
    uint32_t  pcm_samples;
    uint32_t  sample_rate;
    uint8_t   bit_depth;
    uint8_t   channels;
    uint32_t  timestamp_ms;
} AudioAiItem;

static QueueHandle_t s_audio_ai_queue = nullptr;

// ---------------------------------------------------------------------------
// JSON 传感器解析
// ---------------------------------------------------------------------------
static void parse_sensor_json(const char* json_str) {
    cJSON* root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed: %.128s...", json_str);
        return;
    }

    cJSON* radar = cJSON_GetObjectItem(root, "radar");
    if (radar && cJSON_IsObject(radar)) {
        cJSON* exist  = cJSON_GetObjectItem(radar, "exist");
        cJSON* motion = cJSON_GetObjectItem(radar, "motion");
        cJSON* hr     = cJSON_GetObjectItem(radar, "hr");
        cJSON* br     = cJSON_GetObjectItem(radar, "br");
        cJSON* dist   = cJSON_GetObjectItem(radar, "dist");
        SleepDataCenter::GetInstance().UpdateRadar(
            exist  ? exist->valueint  : 0,
            motion ? motion->valueint : 0,
            hr     ? hr->valueint     : 0,
            br     ? br->valueint     : 0,
            dist   ? dist->valueint   : 0);
    }

    cJSON* temp  = cJSON_GetObjectItem(root, "temp");
    cJSON* humi  = cJSON_GetObjectItem(root, "humi");
    cJSON* light = cJSON_GetObjectItem(root, "light_raw");
    if ((temp && cJSON_IsNumber(temp)) || (humi && cJSON_IsNumber(humi))) {
        SleepDataCenter::GetInstance().UpdateEnvironment(
            temp  ? temp->valuedouble  : 0.0f,
            humi  ? humi->valuedouble  : 0.0f,
            light ? light->valueint    : 0);
    }

    cJSON* audio_db = cJSON_GetObjectItem(root, "audio_db");
    cJSON* vad      = cJSON_GetObjectItem(root, "vad");
    if (audio_db && cJSON_IsNumber(audio_db)) {
        SleepDataCenter::GetInstance().UpdateAudioSummary(
            audio_db->valuedouble,
            vad ? vad->valueint : 0);
    }

    cJSON* spo2 = cJSON_GetObjectItem(root, "spo2");
    if (spo2 && cJSON_IsNumber(spo2)) {
        SleepDataCenter::GetInstance().UpdateSpo2((float)spo2->valuedouble);
    }

    cJSON_Delete(root);
    sleep_sd_logger_write(json_str);
}

// ---------------------------------------------------------------------------
// PCM RMS 能量 (dB SPL 近似)
// ---------------------------------------------------------------------------
static float calc_rms_db(const int16_t* samples, int count) {
    if (count <= 0) return -100.0f;
    float sum_sq = 0.0f;
    for (int i = 0; i < count; i++) {
        float s = samples[i] / 32768.0f;
        sum_sq += s * s;
    }
    float rms = sqrtf(sum_sq / count);
    return 20.0f * log10f(rms + 1e-10f) + 94.0f;
}

// ---------------------------------------------------------------------------
// 音频推理：1 秒窗口，50% 重叠，取 max_prob
// ---------------------------------------------------------------------------
static float infer_snore_from_pcm(const int16_t* pcm, int total_samples) {
    const int window_samples = 16000;
    const int hop_samples    = 8000;
    float features[AUDIO_FEATURE_SIZE];
    float max_prob = 0.0f;
    int window_count = 0;

    for (int start = 0; start < total_samples; start += hop_samples) {
        int avail = total_samples - start;
        if (avail < window_samples / 2) break;

        int this_win = (avail >= window_samples) ? window_samples : avail;
        extract_audio_features(pcm + start, this_win, features);

        float prob = 0.0f;
        if (model_interface_invoke(features, &prob)) {
            if (prob > max_prob) max_prob = prob;
            window_count++;
        }
    }

    ESP_LOGI(TAG, "PCM推理: samples=%d windows=%d max_prob=%.3f",
             total_samples, window_count, max_prob);
    return max_prob;
}

// ---------------------------------------------------------------------------
// audio_ai_task：Core 0，大栈，从队列取 PCM 推理
// ---------------------------------------------------------------------------
static void audio_ai_task(void* pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "audio_ai_task started core=%d prio=%d stack_hwm=%d",
             xPortGetCoreID(), uxTaskPriorityGet(NULL),
             uxTaskGetStackHighWaterMark(NULL));

    AudioAiItem item;
    while (true) {
        if (xQueueReceive(s_audio_ai_queue, &item, portMAX_DELAY) == pdPASS) {
            if (!item.pcm || item.pcm_samples == 0) {
                if (item.pcm) heap_caps_free(item.pcm);
                continue;
            }

            // 强制校验音频格式
            if (item.sample_rate != 16000 || item.bit_depth != 16 || item.channels != 1) {
                ESP_LOGW(TAG, "格式不符 sr=%u bd=%u ch=%u，丢弃",
                         item.sample_rate, item.bit_depth, item.channels);
                heap_caps_free(item.pcm);
                continue;
            }

            uint32_t t0 = esp_timer_get_time() / 1000;
            float snore_prob = infer_snore_from_pcm(item.pcm, item.pcm_samples);
            float rms_db = calc_rms_db(item.pcm, item.pcm_samples);
            uint32_t duration_ms = item.pcm_samples * 1000u / item.sample_rate;

            ESP_LOGD(TAG, "AI结果 prob=%.3f rms=%.1fdB dur=%lums cost=%lums",
                     snore_prob, rms_db, duration_ms,
                     (unsigned long)(esp_timer_get_time() / 1000 - t0));

            if (snore_prob >= SNORE_PROB_THRESHOLD) {
                SleepDataCenter::GetInstance().AddSnoreEvent(
                    item.timestamp_ms, duration_ms, snore_prob, rms_db);
                ESP_LOGW(TAG, "=> 鼾声事件 prob=%.3f ts=%lu", snore_prob, item.timestamp_ms);
            }

            heap_caps_free(item.pcm);
        }
    }
}

// ---------------------------------------------------------------------------
// 混合流解析器
// ---------------------------------------------------------------------------
static void process_mixed_stream(uint8_t* rx_buf, int& rx_pos) {
    int processed = 0;

    while (processed < rx_pos) {
        // ---------- 优先检测二进制帧头 0xAA 0x55 ----------
        if (rx_pos - processed >= 2 && rx_buf[processed] == 0xAA && rx_buf[processed + 1] == 0x55) {
            if (rx_pos - processed < 3) break;
            uint8_t frame_type = rx_buf[processed + 2];

            if (frame_type == 0x01) {
                // 音频帧：头 10 字节 + PCM + 1 字节 checksum
                if (rx_pos - processed < 10) break;

                uint16_t sample_rate = rx_buf[processed + 3] | (rx_buf[processed + 4] << 8);
                uint8_t  bit_depth   = rx_buf[processed + 5];
                uint8_t  channels    = rx_buf[processed + 6];
                uint32_t pcm_bytes   = rx_buf[processed + 7]
                                     | (rx_buf[processed + 8] << 8)
                                     | (rx_buf[processed + 9] << 16);

                uint32_t frame_total = 10 + pcm_bytes + 1;
                if (rx_pos - processed < (int)frame_total) break;

                // 和校验
                uint8_t calc_sum = 0;
                for (uint32_t i = 0; i < 10 + pcm_bytes; i++) {
                    calc_sum += rx_buf[processed + i];
                }
                uint8_t recv_sum = rx_buf[processed + 10 + pcm_bytes];

                if (calc_sum == recv_sum && pcm_bytes > 0 && pcm_bytes <= MAX_PCM_BYTES) {
                    // 睡眠监测未开启时，只解析但不投递音频帧
                    if (!sleep_monitor_is_running()) {
                        ESP_LOGD(TAG, "睡眠监测未开启，丢弃音频帧 %lu bytes", pcm_bytes);
                        s_stat_drop_count++;
                        processed += frame_total;
                        continue;
                    }
                    int16_t* pcm_buf = (int16_t*)heap_caps_malloc(pcm_bytes,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (!pcm_buf) {
                        pcm_buf = (int16_t*)heap_caps_malloc(pcm_bytes,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                    }
                    if (pcm_buf) {
                        memcpy(pcm_buf, &rx_buf[processed + 10], pcm_bytes);
                        AudioAiItem item;
                        item.pcm          = pcm_buf;
                        item.pcm_samples  = pcm_bytes / (bit_depth / 8);
                        item.sample_rate  = sample_rate;
                        item.bit_depth    = bit_depth;
                        item.channels     = channels;
                        item.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
                        if (xQueueSend(s_audio_ai_queue, &item, 0) != pdPASS) {
                            ESP_LOGW(TAG, "队列满，丢弃音频帧 %lu bytes", pcm_bytes);
                            heap_caps_free(pcm_buf);
                        } else {
                            ESP_LOGD(TAG, "投递音频帧 %lu bytes sr=%u bd=%u ch=%u",
                                     pcm_bytes, sample_rate, bit_depth, channels);
                            s_stat_audio_count++;
                        }
                    } else {
                        ESP_LOGE(TAG, "PCM内存分配失败 %lu bytes", pcm_bytes);
                    }
                } else {
                    ESP_LOGW(TAG, "校验失败 calc=0x%02X recv=0x%02X bytes=%lu",
                             calc_sum, recv_sum, pcm_bytes);
                }
                processed += frame_total;
            }
            else if (frame_type == 0x02) {
                if (rx_pos - processed < 5) break;
                ESP_LOGD(TAG, "收到句尾标记");
                processed += 5;
            }
            else {
                processed += 2;
            }
        }
        // ---------- JSON 文本 ----------
        else {
            int nl_pos = -1;
            for (int i = processed; i < rx_pos; i++) {
                if (rx_buf[i] == '\n') {
                    nl_pos = i;
                    break;
                }
            }
            if (nl_pos < 0) break;

            int line_start = processed;
            int line_len   = nl_pos - line_start;
            if (line_len > 0 && rx_buf[nl_pos - 1] == '\r') line_len--;

            if (line_len > 0 && line_len < MAX_JSON_LINE) {
                char json_buf[MAX_JSON_LINE];
                memcpy(json_buf, &rx_buf[line_start], line_len);
                json_buf[line_len] = '\0';
                ESP_LOGD(TAG, "JSON: %s", json_buf);
                s_stat_json_count++;
                parse_sensor_json(json_buf);
            }
            processed = nl_pos + 1;
        }
    }

    // memmove 未处理数据
    if (processed > 0 && rx_pos > processed) {
        memmove(rx_buf, rx_buf + processed, rx_pos - processed);
        rx_pos -= processed;
    } else if (processed > 0) {
        rx_pos = 0;
    }
}

// ---------------------------------------------------------------------------
// UART 接收任务：Core 0，栈 8192，优先级 3
// ---------------------------------------------------------------------------
static void uart_mixed_rx_task(void* pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "uart_rx started core=%d prio=%d baud=%d stack_hwm=%d",
             xPortGetCoreID(), uxTaskPriorityGet(NULL), MIXED_UART_BAUD,
             uxTaskGetStackHighWaterMark(NULL));

    uint8_t read_buf[READ_BUF_SIZE];
    s_stat_last_ms = (uint32_t)(esp_timer_get_time() / 1000);

    while (true) {
        int len = uart_read_bytes(MIXED_UART_NUM, read_buf, sizeof(read_buf), pdMS_TO_TICKS(50));
        if (len > 0) {
            if (s_rx_pos + len > RX_BUF_SIZE) {
                ESP_LOGW(TAG, "RX buf溢出 %d+%d > %d，清空缓冲", s_rx_pos, len, RX_BUF_SIZE);
                s_rx_pos = 0;
            }
            if (len <= RX_BUF_SIZE) {
                memcpy(s_rx_buf + s_rx_pos, read_buf, len);
                s_rx_pos += len;
                process_mixed_stream(s_rx_buf, s_rx_pos);
            }
        }

        // 每 5 秒打印一次接收统计
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (now_ms - s_stat_last_ms >= 5000) {
            ESP_LOGI(TAG, "UART stats: json=%lu audio=%lu drop=%lu buf=%d stack=%d",
                     (unsigned long)s_stat_json_count,
                     (unsigned long)s_stat_audio_count,
                     (unsigned long)s_stat_drop_count,
                     s_rx_pos,
                     uxTaskGetStackHighWaterMark(NULL));
            s_stat_json_count = 0;
            s_stat_audio_count = 0;
            s_stat_drop_count = 0;
            s_stat_last_ms = now_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ---------------------------------------------------------------------------
// 启动入口
// ---------------------------------------------------------------------------
bool uart_audio_rx_start(void) {
    if (!model_interface_init()) {
        ESP_LOGW(TAG, "model_interface_init failed, mock fallback");
    }

    s_audio_ai_queue = xQueueCreate(AUDIO_AI_QUEUE_LEN, sizeof(AudioAiItem));
    if (!s_audio_ai_queue) {
        ESP_LOGE(TAG, "audio_ai_queue 创建失败");
        return false;
    }

    // PSRAM 分配 128KB 接收缓冲
    if (!s_rx_buf) {
        s_rx_buf = (uint8_t*)heap_caps_calloc(1, RX_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_rx_buf) {
            s_rx_buf = (uint8_t*)heap_caps_calloc(1, RX_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!s_rx_buf) {
            ESP_LOGE(TAG, "RX buf 分配失败 %d bytes", RX_BUF_SIZE);
            return false;
        }
    }
    s_rx_pos = 0;

    uart_config_t uart_config = {
        .baud_rate = MIXED_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(MIXED_UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return false;
    }

    err = uart_set_pin(MIXED_UART_NUM,
                       MIXED_UART_TX_PIN, MIXED_UART_RX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return false;
    }

    err = uart_driver_install(MIXED_UART_NUM, 4096, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return false;
    }

    // audio_ai_task: Core 0, 栈 16384, 优先级 3
    BaseType_t r1 = xTaskCreatePinnedToCore(
        audio_ai_task, "audio_ai", 16384, NULL, 3, NULL, 0);
    if (r1 != pdPASS) {
        ESP_LOGE(TAG, "audio_ai_task 创建失败");
        return false;
    }

    // uart_mixed_rx_task: Core 0, 栈 8192, 优先级 3
    BaseType_t r2 = xTaskCreatePinnedToCore(
        uart_mixed_rx_task, "uart_rx", 8192, NULL, 3, NULL, 0);
    if (r2 != pdPASS) {
        ESP_LOGE(TAG, "uart_rx_task 创建失败");
        return false;
    }

    ESP_LOGI(TAG, "uart_audio_rx_start OK");
    return true;
}
